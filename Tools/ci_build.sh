#!/bin/bash
set -e

# Clone BuildTools if not already present
if [ ! -d "C:/BuildTools" ]; then
    git clone https://github.com/gildor2/BuildTools.git C:/BuildTools
fi

# Setup wrapper for vc32tools to use the pre-initialized MSVC environment
cat << 'EOF' > C:/BuildTools/bin/vc32tools
#!/bin/bash
found_vc=17
found_vc_year=2022
workpath="${VCINSTALLDIR:-C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC}"
workpath="${workpath//\\//}"

Make() {
    local mkfile="$1"
    shift
    if [ -f "C:/BuildTools/bin/jom.exe" ]; then
        C:/BuildTools/bin/jom.exe -nologo -f "$mkfile" "$@"
    else
        nmake -nologo -f "$mkfile" "$@"
    fi
}

while [ "$1" ]; do
    case "$1" in
        --check)
            break
            ;;
        --make)
            shift
            Make "$@"
            exit $?
            ;;
        *)
            shift
            ;;
    esac
done
EOF

chmod +x C:/BuildTools/bin/vc32tools

export PATH="/c/BuildTools/bin:$PATH"

./build.sh --64
