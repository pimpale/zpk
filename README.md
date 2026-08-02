# zpk

zpk is a dead simple package manager based around the humble `.zip`. Instead of maintaining its own database and manifest, it relies on the Central Directory File present in every zip file, which allows fast enumeration of what files are in a zip. This means that the packaging format doesn't need a manifest or any other metadata. Packages can simply be produced by zipping together a set of files, and assuming they will be unpacked at the root of the filesystem.

In theory, such packages could be installed with a simple `unzip -d / package.zip`, but zpk provides several other utilities that a mere unzip does not, since it maintains all original .zips in a cache (stored at `/pkg` by default). This enables:

1. Uninstalling packages. zpk stores every package in a cache. If an uninstall is desired, it is simple to enumerate which files the package owns, and delete them. 
2. Upgrading packages, including removing files that are no longer present in the new package. One can also do a full system upgrade, which compares zip creation dates to determine which packages to update.
2. Querying which package a file belongs to (eg, `pacman -Qo`).

## Usage
zpk mimics apk's interface, but implements only a subset of the functionality:
```
usage: zpk [options] <command> [args]

commands:
  add <pkg>...             install packages
  del <pkg>...             uninstall packages
  fetch [-o DIR] <pkg>...  download packages without installing
  upgrade [pkg...]         upgrade packages (all if none given)
  fix [pkg...]             reinstall broken packages (all if none given)
  list [-I] [-u] [-a]      list installed/upgradable/available packages
  info -W <path>           show which package owns a path

global options:
  -p, --root DIR           install to alternate root
  -X, --repository URI     add a repository (repeatable)
      --config FILE        use FILE instead of searching for .zpk.ini
  -s, --simulate           simulate the operation; make no changes
  -v, --verbose            raise log level to info; -vv for debug
  -h, --help               show this help
```

## Unsupported

* zpk does not do dependency management. Without a manifest, there is no way to handle this. Packages must vendor their deps or be statically linked.
* zpk is not a transactional package manager, but half-installed packages can be reinstalled without issue.
* zpk does not run preinstall or postinstall scripts (Without a manifest, there is no way to specify these.)

## Why?

This package manager is designed for bare-bones distros and new kernels. We want to reduce the number of operations to the minimum, so that it is trivial to port. We also want to make it a good choice for bootstrapping a system: assembling a filesystem that will be turned into the .iso. 

zpk is easy to port:
* Portable to both POSIX and Windows, natively
* No symlinks (can be implemented on FAT32)
* Keeps all non-stdlib functionality (eg, getcwd) in a single file.

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