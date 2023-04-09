#!/usr/bin/env bash

GIT_DESCRIPTION="$(git describe --dirty --always --tags 2> /dev/null)"
GIT_TAG="$(git describe --exact-match --tags 2> /dev/null)"
GIT_SHA="$(git rev-parse HEAD 2> /dev/null)"
GIT_BRANCH="$(git rev-parse --abbrev-ref HEAD 2> /dev/null)"

if [[ -z "$GIT_TAG" ]]; then
  GIT_TAG="N/A"
fi
if [[ -z "$GIT_DESCRIPTION" ]]; then
  GIT_DESCRIPTION="N/A"
fi
if [[ -z "$GIT_SHA" ]]; then
  GIT_SHA="N/A"
fi
if [[ -z "$GIT_BRANCH" ]]; then
  GIT_BRANCH="N/A"
fi


echo "#pragma once"
echo "# Note: this is an automatically generated file, do not edit"
echo "#define GIT_TAG         \"$GIT_TAG\""
echo "#define GIT_DESCRIPTION \"$GIT_DESCRIPTION\""
echo "#define GIT_SHA         \"$GIT_SHA\""
echo "#define GIT_BRANCH      \"$GIT_BRANCH\""
