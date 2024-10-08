# README

## Project Overview

This program downloads segments of a PNG image from multiple servers using multi-threading, reconstructs the image, and saves it as a single file. The program also validates the PNG file format and handles network communication using `libcurl` for HTTP requests. Once all the segments are received, the image is stitched together and saved to disk.

## Features
- Multi-threaded downloading of image segments from multiple servers.
- Uses `libcurl` for HTTP requests to fetch image segments.
- Validates and processes PNG file headers and chunks.
- Reconstructs PNG images from individual segments.
- Saves the reconstructed image to disk and deletes intermediate segment files.
- Handles errors like memory allocation failures, file operations, and invalid PNG data.

## Prerequisites

- **C Compiler (gcc, clang, etc.)**
- **libcurl**: Used for making HTTP requests.
- **zlib**: Required for decompression of PNG image data.
- **pthread**: For multi-threading functionality.
  
To install these dependencies on Ubuntu:
```bash
sudo apt-get install libcurl4-openssl-dev zlib1g-dev
```

## Compilation

To compile the program, use the following command:
```bash
gcc -o image_downloader main.c -lcurl -lpthread -lz
```

Ensure that the files `crc.h`, `zutil.h`, `lab_png.h`, and `png_util.c` are included in the same directory as the main code.

## Usage

### Command-Line Options

```bash
./image_downloader [-t <num_threads>] [-n <image_num>]
```

- **-t**: Specifies the number of threads to use (1-20). Default is 1.
- **-n**: Specifies the image number to download (1-3). Default is 1.

### Example

Download image segments using 5 threads from image number 2:

```bash
./image_downloader -t 5 -n 2
```

The program will retrieve the image segments from multiple servers and save the final image as `all.png` in the working directory.

## Functionality Overview

1. **Multi-threaded Image Download**:
   - Each thread downloads segments of the image from one of three servers in a round-robin manner.
   - The program uses `libcurl` for HTTP requests and retrieves segments specified by the server in the headers.
   - Downloaded segments are saved in memory and are stitched together once all segments are received.

2. **PNG Processing**:
   - The program validates each PNG segment by checking the file signature.
   - Extracts PNG image properties such as width and height from the IHDR chunk.
   - Handles decompression of PNG data using zlib functions (`mem_def`, `mem_inf`).

3. **Reconstruction and Saving**:
   - After all image segments are downloaded, the program reconstructs the entire PNG image.
   - The reconstructed image is compressed and saved as `all.png`.
   - Temporary segment files are deleted once the final image is created.

## Error Handling

The program includes error handling for:
- Memory allocation failures.
- Network request failures (e.g., `curl_easy_perform` errors).
- PNG validation and CRC checks.
- File I/O errors (e.g., inability to open, write, or delete files).

## Output

- The final reconstructed image is saved as `all.png`.
- Temporary segment files are saved and deleted once the final image is created.

## License

This project is licensed under the MIT License.

---

