#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
WRITERAPP_HOME=$(pwd)
TOOLCHAIN_LIBS="/usr/local/arm-cross-compiler/install/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu"

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j2 Image
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j2 all
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j2 modules
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j2 dtbs

fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p ${OUTDIR}/rootfs && cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cp "${OUTDIR}/linux-stable/arch/arm64/boot/Image" "${OUTDIR}/rootfs"

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
# 1 - Make busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} -j2

# 2 - Install busybox
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
cp ${TOOLCHAIN_LOCATION}/libc/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib
cp ${TOOLCHAIN_LOCATION}/libc/lib64/libm.so.6           ${OUTDIR}/rootfs/lib64
cp ${TOOLCHAIN_LOCATION}/libc/lib64/libresolv.so.2      ${OUTDIR}/rootfs/lib64
cp ${TOOLCHAIN_LOCATION}/libc/lib64/libc.so.6           ${OUTDIR}/rootfs/lib64

# TODO: Make device nodes
cd "${OUTDIR}/rootfs/dev"
sudo mknod -m 666 null c 1 3
sudo mknod -m 600 console c 5 1

# TODO: Clean and build the writer utility
cd ${WRITERAPP_HOME}
make clean
make CROSS_COMPILE=${CROSS_COMPILE}
cp writer ${OUTDIR}/rootfs/home

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
# Copy your finder.sh, conf/username.txt, conf/assignment.txt and finder-test.sh
# scripts from Assignment 2 into the outdir/rootfs/home directory.
cp autorun-qemu.sh "${OUTDIR}/rootfs/home"
cp finder-test.sh "${OUTDIR}/rootfs/home"
cp finder.sh "${OUTDIR}/rootfs/home"
cp ../conf/* "${OUTDIR}/rootfs/home"

sed -i 's|username=$(cat conf/username.txt)|username=$(cat /home/username.txt)|' "${OUTDIR}/rootfs/home/finder-test.sh"
sed -i 's|assignment=`cat ../conf/assignment.txt`|assignment=`cat /home/assignment.txt`|' "${OUTDIR}/rootfs/home/finder-test.sh"

echo "#!/bin/sh

/bin/sh" > "${OUTDIR}/rootfs/initramfs"

chmod +x "${OUTDIR}/rootfs/initramfs"

# TODO: Chown the root directory
cd ${OUTDIR}/rootfs
sudo chown root:root -R *

# TODO: Create initramfs.cpio.gz
sudo find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
