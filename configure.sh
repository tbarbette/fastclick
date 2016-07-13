./configure --with-netmap --disable-netmap-pool --enable-multithread --disable-linuxmodule --enable-intel-cpu --enable-user-multithread --verbose --enable-select=poll CFLAGS="-O1 -g" CXXFLAGS="-std=gnu++11 -O1 -g" --enable-poll --enable-local --enable-zerocopy --enable-batch
make
