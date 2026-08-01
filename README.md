# zipkg

zipkg is a dead simple package manager based around the humble `.zip`. Instead of maintaining its own database and manifest, it relies on the Central Directory File present in every zip file, which allows fast enumeration of what files are in a zip. This means that the packaging format doesn't need a manifest or any other metadata. Packages can simply be produced by zipping together a set of files, and assuming they will be unpacked at the root of the filesystem.

In theory, such packages could be installed with a simple `unzip -d / package.zip`, but zipkg provides several other utilities that a mere unzip does not, since it maintains all original .zips in a cache (stored at `/pkg` by default). This enables:

1. Uninstalling packages. zipkg stores every package in a cache. If an uninstall is desired, it is simple to enumerate which files the package owns, and delete them. 
2. Upgrading packages, including removing files that are no longer present in the new package. One can also do a full system upgrade, which compares zip creation dates to determine which packages to update.
2. Querying which package a file belongs to (eg, `pacman -Qo`).

zipkg also allows configuring a list of repositories.

## Unsupported

* zipkg does not do dependency management. Without a manifest, there is no way to handle this. Packages must vendor their deps or be statically linked.
* zipkg is not a transactional package manager, but half-installed packages can be reinstalled without issue.
* zipkg does not run preinstall or postinstall scripts (Without a manifest, there is no way to specify these.)

## Why?

This package manager is designed for bare-bones distros and new kernels. We want to reduce the number of operations to the minimum, so that it is trivial to port. We also want to make it a good choice for bootstrapping a system: assembling a filesystem that will be turned into the .iso. 

zipkg is easy to port:
* Does not use symlinks (can be implemented on FAT32)
* Does not use fork (can be implemented on a SASOS)


## Implementation Details

Your system should look like this:

```
/usr
/whatever
/pkg      ; contains only installed packages
/pkg/somepackage1.zip
/pkg/somepackage2.zip
/cache/
```

When we search f