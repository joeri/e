#!/bin/bash

#FIXME: refactor windows/linux code

_download()
{
  # Download external libraries
  echo "Downloading external libraries..."
  echo
  pushd arch

  tempdir=`mktemp -d back.XXXXXX` || exit 1
  mv *.zip $tempdir
  mv *.gz $tempdir
  mv *.bz2 $tempdir

  wget http://curl.haxx.se/download/curl-7.18.2.tar.gz
  wget http://libtomcrypt.com/files/crypt-1.11.tar.bz2
  wget http://math.libtomcrypt.com/files/ltm-0.39.tar.bz2
  wget http://www.equi4.com/pub/mk/metakit-2.4.9.7.tar.gz
  wget ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-7.6.tar.gz
  wget http://kent.dl.sourceforge.net/sourceforge/tinyxml/tinyxml_2_5_3.tar.gz
  wget http://biolpc22.york.ac.uk/pub/2.8.10/wxWidgets-2.8.10.tar.bz2

  popd
}

_backup_existing_patches()
{
  # Removing previous folders
  tempdir=`mktemp -d back.XXXXXX` || exit 1
  echo "Moving existing folders to $tempdir"
  mv curl $tempdir/curl
  mv libtomcrypt $tempdir/libtomcrypt
  mv libtommath $tempdir/libtommath
  mv metakit $tempdir/metakit
  mv pcre $tempdir/pcre
  mv tinyxml $tempdir/tinyxml
}

_extract_and_patch()
{
  # Extract
  echo "Extracting libraries.."
  echo
  tar -xzf arch/curl-*
  tar -xjf arch/crypt-*
  tar -xjf arch/ltm-*
  tar -xzf arch/metakit-*
  tar -xzf arch/pcre-*
  tar -xzf arch/tinyxml_*
  tar -xjf arch/wxWidgets-*

  # Rename directories to generic names
  echo "Renaming dirs..."
  echo
  mv curl-* curl
  mv libtomcrypt-* libtomcrypt
  mv libtommath-* libtommath
  mv metakit-* metakit
  mv pcre-* pcre
  mv wxWidgets-* wxwidgets

  # Apply patches
  echo "Applying patches..."
  echo
  patch -d libtomcrypt/src/headers < patches/libtomcrypt.patch
  patch -Np1 -d metakit < patches/metakit.patch
  patch -d pcre < patches/pcre.patch
  patch tinyxml/tinyxml.cpp < patches/tinyxml/tinyxml.cpp.patch
  patch tinyxml/tinyxml.h < patches/tinyxml/tinyxml.h.patch
  patch wxwidgets/src/aui/auibook.cpp < patches/wxwidgets/auibook.cpp.patch
  patch wxwidgets/include/wx/aui/auibook.h < patches/wxwidgets/auibook.h.patch

  # Copy msvc specific project files
  echo "Copying msvc specific project files..."
  echo
  cp build_msvc/curllib* curl/lib
  cp build_msvc/libtomcrypt* libtomcrypt
  cp build_msvc/libtommath* libtommath
  cp build_msvc/tinyxml/* tinyxml
  cp -r build_msvc/metakit/* metakit/win
  cp -r build_msvc/pcre/* pcre
}

_next_steps()
{
  echo "** The libraries have been downloaded and patched. **"
  echo "Now build the following projects in Visual Studio:"
  echo
  echo "curl\lib\curllib.sln"
  echo "libtomcrypt\libtomcrypt.sln"
  echo "libtommath\libtommath.sln"
  echo "metakit\win\msvc90\mksrc.sln"
  echo "pcre\pcre.sln"
  echo "tinyxml\tinyxml.sln"
  echo "wxwidgets\build\msw\wx.dsw"
  echo
  echo "For an automated build, run build_externals_win.cmd in a Visual Studio 2008 Command Prompt."
}


if [[ ! -e arch ]]; then
    mkdir arch
fi

if [[ "$1" != "repatch" ]]; then
      _download
fi

_backup_existing_patches
_extract_and_patch
_next_steps

exit 0
