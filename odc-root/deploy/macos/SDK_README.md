# macOS SDK — Legal Requirements and Extraction Guide

## Legal Notice

The macOS SDK is Apple intellectual property protected by the Xcode license agreement
(https://www.apple.com/legal/sla/docs/xcode.pdf). You **must** accept the Xcode license
terms before extracting the SDK.

The SDK **cannot** be redistributed. Each developer must obtain it themselves.

## Option 1: Extract SDK from Xcode (on a real Mac)

On a Mac with Xcode installed, clone osxcross and run:

```bash
git clone https://github.com/tpoechtrager/osxcross
cd osxcross

# This extracts the SDK from your installed Xcode:
./tools/gen_sdk_package.sh

# This produces a tarball like MacOSX14.0.sdk.tar.xz
ls -lh MacOSX*.sdk.tar.xz
```

Copy the resulting tarball to your Linux build machine.

## Option 2: Extract SDK from Xcode .xip (on Linux)

Download Xcode from https://developer.apple.com/download/all/ (requires Apple ID).

On your Linux machine:

```bash
# Requires: xar, pbzx, cpio (install via apt)
sudo apt-get install -y xar cpio

git clone https://github.com/tpoechtrager/osxcross
cd osxcross

# Extract the SDK from the Xcode .xip:
./tools/gen_sdk_package_pbzx.sh /path/to/Xcode.xip

# This produces MacOSX14.0.sdk.tar.xz
```

Note: This method requires ~45 GB of free disk space during extraction.

## Option 3: Extract SDK from Command Line Tools (on Linux)

Download "Command Line Tools for Xcode" from https://developer.apple.com/download/all/

```bash
git clone https://github.com/tpoechtrager/osxcross
cd osxcross

./tools/gen_sdk_package_tools_dmg.sh /path/to/Command_Line_Tools_for_Xcode.dmg
```

Note: Command Line Tools SDK does not include all macOS frameworks (e.g., SystemConfiguration).
odc-hook does not use these frameworks, so this SDK is sufficient.

## Building the Docker image

Once you have the SDK tarball:

```bash
# Place the SDK tarball in the macos/ directory adjacent to the Dockerfile:
cp MacOSX14.0.sdk.tar.xz odc-root/deploy/macos/

# Build the Docker image:
cd odc-root
docker build \
    --build-arg SDK_TARBALL=MacOSX14.0.sdk.tar.xz \
    --build-arg MACOS_SDK_VERSION=14.0 \
    -t odc-hook-macos -f deploy/macos/Dockerfile .

# Extract the binary:
docker run --rm -v $(pwd)/deploy/output/macos:/output odc-hook-macos

# Output files:
#   deploy/output/macos/odc-hook              # binary
#   deploy/output/macos/odc-hook.yaml         # config template
#   deploy/output/macos/com.odc.hook.plist    # launchd plist
#   deploy/output/macos/install.sh            # install script
```

## Build without Docker (native osxcross)

If you have osxcross installed natively:

```bash
export PATH=/opt/osxcross/bin:$PATH
cmake -B build-macos -GNinja \
    -DCMAKE_TOOLCHAIN_FILE=deploy/macos/toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ODC_HOOK=ON
cmake --build build-macos --target odc-hook
```
