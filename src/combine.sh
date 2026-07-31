find . -type f \( -name "*.cpp" -o -name "*.h" \) | while read -r f; do
    rel="${PWD##*/}/${f#./}"
    echo -e "## File: ${rel}\n"
    cat "$f"
    echo -e "\n\n"
done > combined_src.txt