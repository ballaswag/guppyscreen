FROM ubuntu:22.04

RUN DEBIAN_FRONTEND=noninteractive apt-get update && \
    apt-get install -y --no-install-recommends wget build-essential cmake git ca-certificates && update-ca-certificates && \
    apt-get clean all && \
    apt-get -y autoremove

RUN mkdir /toolchains && \
    wget "https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz?revision=972019b5-912f-4ae6-864a-f61f570e2e7e&rev=972019b5912f4ae6864af61f570e2e7e&hash=A973F165C6D012E0738F90FB4A0C2BA7" -O /tmp/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz && \
    wget https://github.com/ballaswag/k1-discovery/releases/download/1.0.0/mips-gcc720-glibc229.tar.gz -O /tmp/mips-gcc720-glibc229.tar.gz && \
    tar -Jxf /tmp/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz -C /toolchains && \
    tar -zxf /tmp/mips-gcc720-glibc229.tar.gz -C /toolchains && \
    rm /tmp/mips-gcc720-glibc229.tar.gz && \
    rm /tmp/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz

ENV PATH=/toolchains/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin:/toolchains/mips-gcc720-glibc229/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
WORKDIR /toolchains
CMD ["/bin/bash"]                                                                                                                                            
