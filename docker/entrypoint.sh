#!/bin/sh
case "$1" in
  py) shift; exec py "$@" ;;
  spinach) shift; exec spinach "$@" ;;
  *) exec py "$@" ;;
esac
