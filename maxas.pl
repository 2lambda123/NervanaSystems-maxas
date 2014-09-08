#!/usr/bin/perl
use strict;
use Cubin;
use MaxAs;
use Data::Dumper;

require 5.10.0;

my $mode = shift;

# List cubin contents
if ($mode =~ /^\-?\-l/i)
{
	my $cubinFile = shift or usage();

	my $cubin = Cubin->new($cubinFile);

	my $kernels = $cubin->listKernels;
	my $symbols = $cubin->listSymbols;

	foreach my $ker (sort keys %$kernels)
	{
		printf "Kernel: %s (Linkage: %s, Params: %d, Size: %d, Registers: %d, SharedMem: %d, Barriers: %d)\n", $ker, @{$kernels->{$ker}}{qw(Linkage ParamCnt size RegCnt SharedSize BarCnt)};
	}
	foreach my $sym (sort keys %$symbols)
	{
		printf "Symbol: %s\n", $sym;
	}
}
# Test that the assembler can reproduce the op codes this cubin or sass contains
elsif ($mode =~ /^\-?\-t/i)
{
	my $file = shift or usage();
	my $fh;
	# sass file
	if (-T $file)
	{
		open $fh, $file or die "$file: $!";
	}
	# cubin file
	else
	{
		open $fh, "cuobjdump.exe -arch sm_50 -sass $file |" or die "cuobjdump.exe -arch sm_50 -sass $file: $!";
		my $first = <$fh>;
		if ($first =~ /cuobjdump fatal/)
		{
			print $first;
			exit(1);
		}
	}
	exit(MaxAs::Test($fh) ? 1 : 0);
}
# Extract an asm file containing the desired kernel
elsif ($mode =~ /^\-?\-e/i)
{
	my $kernelName;
	if ($ARGV[0] =~ /^\-?\-k/i)
	{
		shift;
		$kernelName = shift or usage();
	}
	my $cubinFile = shift or usage();
	my $asmFile   = shift;
	my $cubin     = Cubin->new($cubinFile);
	my $kernels   = $cubin->listKernels;

	#default the kernel name if not specified.
	$kernelName ||= (sort keys %$kernels)[0];

	my $kernel = $kernels->{$kernelName} or die "bad kernel: $kernelName";

	open my $in, "cuobjdump.exe -arch sm_50 -sass -fun $kernelName $cubinFile |" or die "cuobjdump.exe -arch sm_50 -sass -fun $kernelName $cubinFile: $!";
	my $first = <$in>;
	if ($first =~ /cuobjdump fatal/)
	{
		print $first;
		exit(1);
	}
	my $out;
	if ($asmFile)
	{
		open $out, ">$asmFile" or die "$asmFile: $!";
	}
	else
	{
		$out = \*STDOUT;
	}

	print $out "# Kernel: $kernelName\n";

	print $out "# $_: $kernel->{$_}\n" foreach (qw(InsCnt RegCnt SharedSize BarCnt));

	print $out "# Params($kernel->{ParamCnt}):\n#\tord:addr:size:align\n";

	print $out join('', map "#\t$_\n", @{$kernel->{Params}}) if $kernel->{Params};

	print $out "#\n# Instructions:\n\n";

	MaxAs::Extract($in, $out);

	close $out if $asmFile;
	close $in;
}
# Insert the kernel asm back into the cubin:
elsif ($mode =~ /^\-?\-i/i)
{
	my $asmFile   = shift or usage();
	my $cubinFile = shift or usage();
	my $newCubin  = shift || $cubinFile;

	my $file;
	if (open my $fh, $asmFile)
	{
		local $/;
		$file = <$fh>;
		close $fh;
    }
    else { die "$asmFile: $!" }

    # extract the kernel name from the file
	my ($kernelName) = $file =~ /^# Kernel: (\w+)/;
	die "asm file missing kernel name or is badly formatted" unless $kernelName;

	my $kernel = MaxAs::Assemble($file);

	my $cubin  = Cubin->new($cubinFile);
	$kernel->{Kernel} = $cubin->getKernel($kernelName) or die "cubin does not contain kernel: $kernelName";

	$cubin->modifyKernel(%$kernel);

	$cubin->write($newCubin);
}
# Preprocessing:
elsif ($mode =~ /^\-?\-p/i)
{
	my $doReg     = shift if $ARGV[0] =~ /^\-?\-r/i;
	my $asmFile   = shift or usage();
	my $asmFile2  = shift or usage();

	die "source and destination probably shouldn't be the same file\n" if $asmFile eq $asmFile2;

	open my $fh,  $asmFile or die "$asmFile: $!";
	local $/;
	my $file = <$fh>;
    close $fh;

	open $fh, ">$asmFile2" or die "$asmFile2: $!";
	print $fh MaxAs::Preprocess($file, $doReg);
	close $fh;
}
else
{
	usage();
}

exit(0);



sub usage
{
	print <<EOF;
Usage:

  List kernels and symbols:

    maxas.pl --list|-l <cubin_file>

  Test a cubin or sass file to to see if the assembler can reproduce all of the contained opcodes:

    maxas.pl --test|-t <cubin_file | sass_file>

  Extract a single kernel into an asm file from a cubin.
  Works much like cuobjdump but outputs in a format that can be re-assembled back into the cubin:

    maxas.pl --extract|-e [--kernel|-k kernel_name] <cubin_file> [asm_file]

  Preprocess the asm (expand CODE sections, perform scheduling, optionally do register renaming).
  Mainly used for debugging purposes:

    maxas.pl --pre|-p [--reg|-r] <asm_file> <new_asm_file>

  Insert the kernel asm back into the cubin.  Overwrite existing or create new cubin.
  Also does any preprocesing required:

    maxas.pl --insert|-i <asm_file> <cubin_file> [new_cubin_file]

EOF
	exit(1);
}

__END__
