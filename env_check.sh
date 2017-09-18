#!/bin/sh

LIBVIRT_DIR=/home/$USER/libvirt

# check and create libvirt directory and sub dir.

if [ -z $LIBVIRT_DIR ]; then
  if [ -d $LIBVIRT_DIR ]; then
      echo "$LIBVIRT_DIR is not a directory!"
      exit 1
  fi
  # create libvirt dir
  mkdir $LIBVIRT_DIR
  # create qemu sub-dir
  mkdir $LIBVIRT_DIR/qemu
  # create log sub-dir
  mkdir $LIBVIRT_DIR/log

  # create image sub-dir
  mkdir $LIBVIRT_DIR/images
  # maybe the directory is existed so mkdir will return non-zero.
  # reset the exit code to 0 let make know it works correctly.
  exit 0
fi
