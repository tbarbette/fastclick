#!/bin/bash
archs="i386 i486 i586 pentium lakemont pentium-mmx winchip-c6 winchip2 c3 samuel-2 c3-2 nehemiah c7 esther i686 pentiumpro pentium2 pentium3 pentium3m pentium-m pentium4 pentium4m prescott nocona core2 nehalem corei7 westmere sandybridge corei7-avx ivybridge core-avx-i haswell core-avx2 broadwell skylake skylake-avx512 cannonlake icelake-client icelake-server cascadelake tigerlake bonnell atom silvermont slm goldmont goldmont-plus tremont knl knm intel geode k6 k6-2 k6-3 athlon athlon-tbird athlon-4 athlon-xp athlon-mp x86-64 eden-x2 nano nano-1000 nano-2000 nano-3000 nano-x2 eden-x4 nano-x4 k8 k8-sse3 opteron opteron-sse3 athlon64 athlon64-sse3 athlon-fx amdfam10 barcelona bdver1 bdver2 bdver3 bdver4 znver1 znver2 btver1 btver2"
for arch in ${archs} ; do
    if ( gcc -march=$arch -dM -E - < /dev/null | egrep "SSE4_2" ) &> /dev/null ; then
        echo "Building for $arch..."
        docker build -t tbarbette/fastclick-dpdk:$arch --build-arg machine=$arch -f etc/Dockerfile.dpdk .
        #docker tag fastclick-dpdk-$arch tbarbette/fastclick-dpdk:$arch
        docker push tbarbette/fastclick-dpdk:$arch
    fi
done

docker tag tbarbette/fastclick-dpdk:corei7 tbarbette/fastclick-dpdk:generic
docker push tbarbette/fastclick-dpdk:generic
