#!/bin/bash

# Extract short commit hash from Git repository
git_commit=$(git --work-tree=$(dirname "$0") rev-parse --short HEAD 2>/dev/null)

# Check if the Git command was successful
if [[ $? -ne 0 ]]; then
    echo "#define NO_GIT_COMMIT" > "$(dirname "$0")/version.h.tmp"
else
    # Write the commit hash to a temporary file
    echo "#define GIT_COMMIT $git_commit" > "$(dirname "$0")/version.h.tmp"
fi

# Compare the temporary version file with the existing one and update if different
if ! cmp -s "$(dirname "$0")/version.h.tmp" "$(dirname "$0")/version.h"; then
    cp -f "$(dirname "$0")/version.h.tmp" "$(dirname "$0")/version.h"
fi

# Clean up the temporary file
rm -f "$(dirname "$0")/version.h.tmp"
