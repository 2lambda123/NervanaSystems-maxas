// sgemm.cpp : Defines the entry point for the console application.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cuda.h>
#include <cublas_v2.h>

CUcontext      hContext = 0;
cublasHandle_t hCublas  = 0;

float assemblySgemm(const char* kernel, CUdeviceptr devC, CUdeviceptr devA, CUdeviceptr devB, int N, CUevent hStart, CUevent hStop, int repeat = 1, int printVars = 0);
void gflops(const char* ident, int N, float ms, int repeat);
void test(float* C, float* T, int N, size_t size);

#define CUDA_CHECK( fn ) do { \
		CUresult status = (fn); \
		if ( CUDA_SUCCESS != status ) { \
			const char* errstr; \
			cuGetErrorString(status, &errstr); \
			printf("CUDA Driver Failure (line %d of file %s):\n\t%s returned 0x%x (%s)\n", __LINE__, __FILE__, #fn, status, errstr); \
			if (hCublas)  cublasDestroy(hCublas); \
			if (hContext) cuCtxDestroy(hContext); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

#define CUBLAS_CHECK( fn ) do { \
		cublasStatus_t status = (fn); \
		if ( CUBLAS_STATUS_SUCCESS != status ) { \
			printf("Cublas Failure (line %d of file %s):\n\t%s returned %d\n", __LINE__, __FILE__, #fn, status); \
			if (hCublas)  cublasDestroy(hCublas); \
			if (hContext) cuCtxDestroy(hContext); \
			exit(EXIT_FAILURE); \
		} \
	} while (0)

int main(int argc, char* argv[])
{
	char deviceName[32];
	int count, ordinal, major, minor;
	CUdevice  hDevice;
	CUevent hStart, hStop;
	CUdeviceptr devA, devB, devC, devT;

	// Initialize the Driver API and find a device
	CUDA_CHECK( cuInit(0) );
	CUDA_CHECK( cuDeviceGetCount(&count) );
	for (ordinal = 0; ordinal < count; ordinal++)
	{
		CUDA_CHECK( cuDeviceGet(&hDevice, ordinal) );
		CUDA_CHECK( cuDeviceGetAttribute (&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, hDevice) );
		CUDA_CHECK( cuDeviceGetAttribute (&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, hDevice) );
		CUDA_CHECK( cuDeviceGetName(deviceName, sizeof(deviceName), hDevice) );
		if (major >= 5)
		{
			//printf("Using: Id:%d %s (%d.%d)\n\n", ordinal, deviceName, major, minor);
			break;
		}
	}
	if (ordinal == count)
	{
		printf("No compute 5.0 device found, exiting.\n");
		exit(EXIT_FAILURE);
	}

	// First command line arg is the size of N divided by 64
	int thread64 = 80;
	if (argc > 1)
		thread64 = atoi(argv[1]);
	if (thread64 > 80 || thread64 < 1)
		thread64 = 80;

	// Second command line arg is the repeat count for benchmarking
	int repeat = 1;
	if (argc > 2)
		repeat = atoi(argv[2]);
	if (repeat > 1000 || repeat < 1)
		repeat = 1;

	// Third command line arg is for printf debugging
	int printVars = 0;
	if (argc > 3)
		printVars = atoi(argv[3]);
	if (printVars > 100 || printVars < 1)
		printVars = 0;

	int N = thread64 * 64;
	size_t size = sizeof(float) * N * N;
	float alpha = 1, beta = 0, ms = 1;

	float* A = (float*)malloc(size);
	float* B = (float*)malloc(size);
	float* C = (float*)malloc(size);
	float* T = (float*)malloc(size);

	//memset(A, 0, size);
	//memset(B, 0, size); 

	int counter = 0;
	srand((unsigned int)time(0));
	for(int i = 0; i < N * N; i++) //
	{
		A[i] = (float)rand() / (float)RAND_MAX;
		B[i] = (float)rand() / (float)RAND_MAX;
		//A[i] = B[i] = 1.0f; // * (i & 3) + 1.0f;
		//A[i] = 1.0f;
		//B[i * N + counter++] = 1.0f; // identity matrix
	}

	CUDA_CHECK( cuCtxCreate(&hContext, 0, hDevice) );
	CUBLAS_CHECK( cublasCreate(&hCublas) );
	
	CUDA_CHECK( cuEventCreate(&hStart, CU_EVENT_DEFAULT) ); // CU_EVENT_BLOCKING_SYNC 
	CUDA_CHECK( cuEventCreate(&hStop,  CU_EVENT_DEFAULT) );

	CUDA_CHECK( cuMemAlloc(&devA, size) );
	CUDA_CHECK( cuMemAlloc(&devB, size) );
	CUDA_CHECK( cuMemAlloc(&devC, size) );
	CUDA_CHECK( cuMemAlloc(&devT, size) );
	
	CUDA_CHECK( cuMemcpyHtoD(devA, A, size) );
	CUDA_CHECK( cuMemcpyHtoD(devB, B, size) );
	CUDA_CHECK( cuMemsetD8(devC, 0, size) );
	CUDA_CHECK( cuMemsetD8(devT, 0, size) );
	
	// Warm up the clock (unless under nsight)
	if (!getenv("NSIGHT_LAUNCHED")) // NSIGHT_CUDA_ANALYSIS NSIGHT_CUDA_DEBUGGER 
		for (int i = 0; i < 3; i++)
			CUBLAS_CHECK( cublasSgemm(hCublas, CUBLAS_OP_N, CUBLAS_OP_T, N, N, N, &alpha, reinterpret_cast<float*>(devA), N, reinterpret_cast<float*>(devB), N, &beta, reinterpret_cast<float*>(devT), N) );

	// Launch our kernel
	//ms = assemblySgemm("sgemm_kernel_64",  devC, devA, devB, N, hStart, hStop, repeat, printVars);
	//gflops("Max64 ", N, ms, repeat);

	ms = assemblySgemm("sgemm_kernel_128", devC, devA, devB, N, hStart, hStop, repeat, printVars);
	gflops("Max128", N, ms, repeat);

	// Run cublas again for the same repeat count for comparison
	//CUDA_CHECK( cuEventRecord(hStart, NULL) );
	//for (int i = 0; i < 1; i++)
	//	CUBLAS_CHECK( cublasSgemm(hCublas, CUBLAS_OP_N, CUBLAS_OP_T, N, N, N, &alpha, reinterpret_cast<float*>(devA), N, reinterpret_cast<float*>(devB), N, &beta, reinterpret_cast<float*>(devT), N) );
	//CUDA_CHECK( cuEventRecord(hStop, NULL) );
	//CUDA_CHECK( cuEventSynchronize(hStop) );
	//CUDA_CHECK( cuEventElapsedTime(&ms, hStart, hStop) );
	//gflops("Cublas", N, ms, 1);

	// Get back our results from each kernel
	CUDA_CHECK( cuMemcpyDtoH(C, devC, size) );
	CUDA_CHECK( cuMemcpyDtoH(T, devT, size) );
	
	// Cleanup and shutdown of cuda
	CUDA_CHECK( cuMemFree(devA) );
	CUDA_CHECK( cuMemFree(devB) );
	CUDA_CHECK( cuMemFree(devC) );
	CUDA_CHECK( cuMemFree(devT) );

	CUDA_CHECK( cuEventDestroy(hStart) );
	CUDA_CHECK( cuEventDestroy(hStop) );

	CUBLAS_CHECK( cublasDestroy(hCublas) );
	hCublas  = 0;
	CUDA_CHECK( cuCtxDestroy(hContext) );
	hContext = 0;

	// compare C and T for accuracy
	test(C, T, N, size);

	// And free up host memory
	free(A); free(B); free(C); free(T);

	return 0;
}

// Our kernel wrapper function
float assemblySgemm(const char* kernel, CUdeviceptr devC, CUdeviceptr devA, CUdeviceptr devB, int N, CUevent hStart, CUevent hStop, int repeat, int printVars)
{
	// Configure our x and y grid dimensions (assume nice square matrixes).
	// Each block gets 128 tracks from A and 128 tracks from B.
	// Each of the 256 threads calculates 64 elements of that 128x128 sub matrix of C.
	// See Figure 2 here to get the gist of things (we use a different mapping to maximize LDS.128 usage):
	// http://icl.cs.utk.edu/projectsfiles/magma/pubs/fermi_gemm.pdf

	int threads, width;
	if (strcmp(kernel, "sgemm_kernel_64") == 0)
	{
		threads = 64;
		width   = 64;
	}
	else
	{
		threads = 256;
		width   = 128;
	}

	int gridDimXY = N / width + (N % width != 0);
	int blocks    = gridDimXY * gridDimXY;
	size_t size   = sizeof(float)*N*N;

	// Setup out debug printf output buffer
	CUdeviceptr devD = NULL; 
	int* D = NULL;
	int  sizeD = 0;

	if (printVars)
	{
		sizeD = blocks * threads * printVars * sizeof(int);
		D = (int*)malloc(sizeD);

		CUDA_CHECK( cuMemAlloc(&devD, sizeD) );
		CUDA_CHECK( cuMemsetD8(devD, 0, sizeD) );
	}

	// Load the cubin
	CUmodule hModule;
	CUDA_CHECK( cuModuleLoad(&hModule, "sgemm.cubin") );

	// Load the textures
	CUtexref texA, texB;
	CUDA_CHECK( cuModuleGetTexRef(&texA, hModule, "texA") );
	CUDA_CHECK( cuModuleGetTexRef(&texB, hModule, "texB") );

	// Configure the textures
	CUDA_CHECK( cuTexRefSetFormat(texA, CU_AD_FORMAT_FLOAT, 4) );
	CUDA_CHECK( cuTexRefSetFormat(texB, CU_AD_FORMAT_FLOAT, 4) );

	CUDA_CHECK( cuTexRefSetAddress(NULL, texA, devA, size) );
	CUDA_CHECK( cuTexRefSetAddress(NULL, texB, devB, size) );

	// Load the kernel function
	CUfunction hKernel;
	CUDA_CHECK( cuModuleGetFunction(&hKernel, hModule, kernel) );

	// Setup the params
	float alpha = 1.0f;
	void* params[] = { &devC, &N, &N, &N, &N, &N, &N, &alpha, &devD };

	float totalTime = 0;
	// Launch the kernel repeat times.. but break it up into pieces so as not to lock things up.
	while (repeat > 0)
	{
		float ms;
		int r = repeat > 2 ? 2 : repeat;
		CUDA_CHECK( cuEventRecord( hStart, NULL ) );
		
		for (int i = 0; i < r; i++)
			CUDA_CHECK( cuLaunchKernel(hKernel, gridDimXY, gridDimXY, 1, threads, 1, 1, 0, 0, params, 0) );
		
		CUDA_CHECK( cuEventRecord( hStop, NULL ) );
		CUDA_CHECK( cuEventSynchronize( hStop ) );
		CUDA_CHECK( cuEventElapsedTime( &ms, hStart, hStop ) );
		totalTime += ms;
		repeat -= r;
	}


	CUDA_CHECK( cuModuleUnload(hModule) );

	// And here we print out the debug info if requested:
	if (printVars)
	{
		CUDA_CHECK( cuMemcpyDtoH(D, devD, sizeD) );
		CUDA_CHECK( cuMemFree(devD) );
		int   *iD = D;
		float *fD = reinterpret_cast<float*>(D);

		for (int by = 0; by < gridDimXY; by++)
		{
			for (int bx = 0; bx < gridDimXY; bx++)
			{
				for (int tid = 0; tid < threads; tid++)
				{
					//printf("by: %3d, bx: %3d, tid:%3d, rA:%5d, rB:%5d, wr:%5d, rd:%5d, cx:%5d, cy:%5d, ci:%5d, c:%.2f\n", 
					printf("by: %3d, bx: %3d, tid:%3d, t0:%5d, end:%5d, k:%5d, tid2:%5d, tid15:%5d, ldx:%5d, t2:%5d, t4:%5d\n", 
						    by,      bx,      tid,     iD[0],  iD[1],   iD[2], iD[3],    iD[4],     iD[5],   iD[6],  iD[7]
					);
					iD += printVars;
					fD += printVars;
				}
			}
		}
		free(D);
	}

	return totalTime;
}

void gflops(const char* ident, int N, float ms, int repeat)
{
	// Standard sgemm flops formula
	ms /= repeat;
	printf("%s GFLOPS: %.2f (size: %d, iterations: %d)\n", ident, ((double)N * N * N * 2.0) / (ms * 1000000.0), N, repeat);
}

void test(float* C, float* T, int N, size_t size)
{
	// Compare our implementation with the cublas result
	int errors = memcmp(C, T, size);
	if (errors)
	{
		if (N <= 768) // This gets too big and slow for large N
		{
			errors = 0;
			FILE* file;
			if (fopen_s(&file, "data.txt", "w") == 0)
			{
				for (int y = 0; y < N; ++y)
				{
					for (int x = 0; x < N; ++x)
					{
						float c = C[x*N + y];
						float t = T[x*N + y];
						if (c != t)
						{
							errors++;
							//fprintf(file, "%.0f!%.0f\t", c , t);
							fprintf(file, "%.0f!", c);
							//fprintf(file, "!");
						}
						else
						{
							//fprintf(file, "%.0f=%.0f\t", c , t);
							fprintf(file, "%.0f=", c);
							//fprintf(file, "=");
						}
					}
					fprintf(file, "\n");
				}
				fclose(file);
				printf("%d errors\n", errors);
			}
			else
				{ printf("Cannot open data.txt for writing\n"); }
		}
		else
			{ printf("%d errors\n", errors); }
	}
	else
		{ printf("%d errors\n", errors); }
}