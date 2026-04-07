#!/bin/sh

# Script to process JSON braced initializers in source files that are enclosed with // clang-format off /*JSON*/ and // clang-format on

# Function to process a file
process_file() {
  file="$1"
  temp_file="${file}.tmp"
  temp_formatted="${file}.fmt"

  # Remove the off/on lines
  sed '/\/\/ clang-format off \/\*JSON\*\//d; /\/\/ clang-format on/d' "$file" > "$temp_file"

  # Format the file with perfect style for JSON braced initializers
  clang-format --style='{BasedOnStyle: Mozilla, ColumnLimit: 80, SpacesInContainerLiterals: false}' "$temp_file" > "$temp_formatted"

  # Add back the markers around json response = { ... };
  awk -f scripts/format.awk "$temp_formatted" > "$file"

  rm "$temp_file"
  echo "Processed $file"
}

# Process src/main.cpp
process_file "src/main.cpp"

echo "JSON formatting markers added. Run clang-format to format the rest of the code."

