Virtual Block Device
========================

Setup
----------
*   Download vbd

        wget -c https://github.com/truncs/vbd/zipball/master
*   unzip

        unzip truncs-vbd-c7ca87f.zip
*   Make

        cd vbd && make
*   Smoke !

        sudo insmod vbd.ko

Benchmarking
---------------
All benchmarking is done by using iozone3 on Xubuntu 11.10 with the
following

    $uname -a
    Linux sanket-VirtualBox 3.0.0-12-generic #20-Ubuntu SMP Fri Oct 7 14 50:42 UTC 2011 i686 i686 i386 GNU/Linux

The following command was used to do all the benchmarking

    $iozone -I -s 10K -r 1K

The -I argument indicates the filesytem to bypass the buffer cache and do all
io operations directly on the disk
