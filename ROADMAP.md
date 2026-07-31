# ROADMAP

**Everything here is not implemented yet!**

## Maintaining Packages

The other half of package management is the maintainer's job: building the packages and maintaining patches.

We want to provide a simple (but unfortunatley not comprehensive) solution, `mkzipkg`. `mkzipkg` tries to be as simple as possible as well. It doesn't require chroot support. It works just like [bear](https://github.com/rizsotto/Bear/wiki/How-It-Works) in wrapper mode. It provides a wrapper for the unix commands `cp` and `install`, and records where everything is installed to, and then builds a zip from the installed files.