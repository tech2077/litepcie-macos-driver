 #!/bin/bash

set -e 

find -E . -regex '.*\.(h|cpp|hpp|hxx|cxx|iig)' -not -path "./build/*"  -exec clang-format -style=file -i {} \;