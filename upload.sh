#!/bin/bash

if [ $# -lt  1 ] || [ $# -gt 2 ]; then
  echo "This tool will send either one, or all data files to the web interface"
  echo ""
  echo "Syntax: $0 <hostname or IP address> [<path to a file>]"
  exit 255
fi

IP="$1"
echo "Uploading to $IP"

if [ $# -gt 1 ]; then
  files="$2"
else
  # Smart upload: only upload changed files and delete removed ones
  echo "Performing smart upload..."

  # Get list of existing files with sizes from ESP32
  echo "Fetching existing files from ESP32..."
  existing_data=$(curl -s "http://$IP/list")
  existing_files=$(echo "$existing_data" | grep -o '"name":"[^"]*","type":"[^"]*"' | sed 's/"name":"\([^"]*\)","type":"[^"]*"/\1/')

  # Get local file info
  local_files=$(find ./data -type f -print0 | while IFS= read -r -d '' file; do
    relative_path="${file#./data/}"
    basename_path=$(basename "$file")
    size=$(stat -f%z "$file" 2>/dev/null || stat -c%s "$file" 2>/dev/null || echo "0")
    md5=$(md5sum "$file" 2>/dev/null | cut -d' ' -f1 || echo "")
    echo "$relative_path|$basename_path|$size|$md5"
  done)

  # Get remote file info
  remote_files=$(echo "$existing_data" | jq -r '.[] | select(.type == "file") | "\(.name)|\(.size)|\(.md5)"' 2>/dev/null)

  # Find files to upload (new or changed)
  files_to_upload=""

  # Process each local file
  echo "$local_files" | while IFS='|' read -r relative_path basename_path size md5; do
    if [ -n "$relative_path" ]; then
      remote_line=$(echo "$remote_files" | grep "^$basename_path|" | head -1)
      if [ "$remote_line" = "" ]; then
        # File doesn't exist remotely
        echo "New file: $relative_path"
        echo "./data/$relative_path" >> /tmp/upload_files.tmp
      else
        remote_md5=$(echo "$remote_line" | cut -d'|' -f3)
        if [ "$md5" != "$remote_md5" ]; then
          # MD5 differs - upload it
          echo "Changed file: $relative_path"
          echo "./data/$relative_path" >> /tmp/upload_files.tmp
        fi
      fi
    fi
  done

  # Read the upload files list
  if [ -f /tmp/upload_files.tmp ]; then
    files_to_upload=$(cat /tmp/upload_files.tmp | tr '\n' ' ')
    rm /tmp/upload_files.tmp
  fi

  # Find files to delete (exist remotely but not locally)
  files_to_delete=""
  echo "$existing_files" | while read -r remote_file; do
    if ! echo "$local_files" | grep -q "^$remote_file|"; then
      files_to_delete="$files_to_delete $remote_file"
    fi
  done

  # Delete removed files
  if [ -n "$files_to_delete" ]; then
    echo "Deleting obsolete files..."
    for file_to_delete in $files_to_delete; do
      echo "Deleting: $file_to_delete"
      curl -X DELETE "http://$IP/edit?f=$file_to_delete&s=onboard" >/dev/null 2>&dev/null
    done
  fi

  files="$files_to_upload"
fi

# Count files to upload
upload_count=0
if [ -n "$files" ]; then
  upload_count=$(echo "$files" | wc -w)
fi

# Count files to delete
delete_count=0
if [ -n "$files_to_delete" ]; then
  delete_count=$(echo "$files_to_delete" | wc -w)
fi

if [ $upload_count -gt 0 ] || [ $delete_count -gt 0 ]; then
  echo "Summary: $upload_count files to upload, $delete_count files to delete"
else
  echo "All files are synchronized - nothing to do"
fi

for file in $files; do
  # Skip if it's a directory
  if [ -d "$file" ]; then
    continue
  fi
  echo "Sending: $file"
  # Extract path relative to data directory
  relative_path="${file#./data/}"
  # curl -v --trace-ascii - -F 'data=@"'"$file"'";filename="'"$relative_path"'"' http://"$IP"/edit
  if curl -f -F 'data=@"'"$file"'";filename="'"$relative_path"'"' http://"$IP"/edit >/dev/null 2>&1; then
    echo "✓ Upload successful: $relative_path"
  else
    echo "✗ Upload failed: $relative_path"
  fi
done
