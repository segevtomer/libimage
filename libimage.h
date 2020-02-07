#pragma once
#define _CRT_SECURE_NO_DEPRECATE
#pragma warning (disable : 4996)

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <png.h>

#define JPEG "JPEG"
#define PNG "PNG"

#define JPEG_L 4
#define PNG_L 8

/*
* the library's external API (open,save,avg,pave) all return a boolean
* the boolean value indicates whether the call is successful or not
* in the event of success, the functions allocate the request to the last
* input parameter (e.g: openImage allocates and sets the values for argument 'image')
*
*
* general note: whenever multiple loop arguments are called back-to-back instead of using
* an inner loop (which to be fair could look a bit nicer), it is done to take advantage of
* the performance boost of loop-unrolling and the locality principle on the referenced data
*
*
* DISCLAIMER:
* current implementation, due to time constraints, supports only 8 bit-depth RGBA
* PNG images (for average and pave manipulations). support for other variations is
* expandable and the only detriment for it under development was time limitations.
*
* the code was developed and tested using libpng16
*/

enum format
{
    NoneFormat, Png, Jpeg, Bmp, Gif
};

enum colorType
{
    NoneType, GrayScale,GSA,RGB,RGBA,PLTE
};

typedef unsigned char byte;

/*
 * format magic numbers
 */
const byte png[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
const byte jpeg[4] = { 255, 216, 255, 224 };
const byte jpeg2[4] = { 255, 216, 255, 225 };

/*
 * more formats, may be added in the future
 */
const byte bmp[2] = { 42, 40 };
const byte gif[6] = { 47, 49, 46, 38, 39, 61 };
const byte gif2[6] = { 47, 49, 46, 38, 37, 61 };

/*
* the Image struct is polymorphic for any image type, each image-type-handler
* will allocate the data from the specific format type to the Image struct
*/
typedef struct
{
    byte** rowPtrs;
    size_t width;
    size_t height;
    byte bitDepth;
    byte colorTypeVal;
    enum colorType colorTypeEnum;
} Image;


/*
* the Buffer struct is used for read&write capabilities, in order to open the image
* from an in-memory buffer (at least in the specific case of the png-handler), the
* libpng library required me to provide my own I/O procedures - hence Buffer was made
*/
typedef struct
{
    byte* buf;
    size_t size;
} Buffer;


/*
* receives a byte array and a ptr to NULL image ptr, verifies the format
* is supported and redirects it to the relevant format-open-handler
* ret val is indicaction of success, in case of success '*image' will
* be allocated with the image data
*/
bool openImage(byte* buf, Image** image);

/*
* receives an image ptr, format string and NULL ptr of byte array, verifies the save
* format is supported and redirects it to the relevant format-save-handler
* ret val is indicaction of success, in case of success retBuffer will reference the
* address of the byte array that is the binary representation of the Image argument
*/
bool saveImage(Image* image, const char* format, byte** retBuffer);

/*
* receives an image ptr and the requested dimension to be used for the average calculation
* in case of allocation failures the state of the image ptr is unaltered and ret val is false
* in case of success the image ptr is altered with the newly calculated average data
*/
bool averageImage(int avgDim, Image* image);

/*
* receives an image ptr, the requested num of images to split the image in-to
* and the ptr to the (not-yet-allocated and-) soon to be array of chunked images
* the number of images determines how many chunks are to be made (numOfImages^2)
* the leftover edge pixels that do not fit in the fixed dimension (the same for 
* each chunk) are trimmed, leaving a fixed number of symmetrical image chunks.
* in case of success, imageChunks will now point to an array of allocated image
* ptrs where each image ptr holds the data of a chunk of the original image
*/
bool paveImage(int numOfImgs, Image* image, Image*** imageChunks);


/*
* specific handlers for parsing the matrix of image data with 8-bit depth and RGBA channels
* an expandle solution, new handlers will be able to support more bit-depth and color-types
*/
bool handleEightBitRgbaPaving(Image* image, int numOfImgs, Image*** imageChunks);
bool handleEightBitRgbaAveraging(Image* image, int avgDim);

/*
* helper function for calculating the average value for the given dimension.
* rows is the original matrix, avgDim dictates the size of the average matrix, 
* start_x and start_y are coordinates for the origin point to be used in rows
* edge case handling is done outside calcAverage prior to its call in averageImage
* in the scenario of success the averages are returned using the last argument
*/
void calcAverage(byte** rows, int avgDim, size_t start_x, size_t start_y, int* avgs);

/*
* creates and allocates the memory for the averaged binary matrix that represents the averaged image
* in case of success, the matrix is to be returned. in case of failure, return value is NULL
*/
byte** createAvgImage(byte** rows, size_t newHeight, size_t newWidth, int numOfImgs);


/*
* this following 2 procedures override the default I/O procedures for the libpng library
* this is done to allow libpng's API to use my provided buffers instead of the default file stream
*/
void readFromBuffer(png_structp png_ptr, png_bytep dataBuffer, png_size_t bytesToRead);
void writeToBuffer(png_structp png_ptr, png_bytep dataBuffer, png_size_t bytesToRead);


/*
* specific type handlers that are called from their generic external counterparts
* in the case of addition of future formats, each format will receive its own handler
*/
bool handleOpenPng(byte* buf, Image** image);
bool handleSavePng(Image* image, byte** buf);
/* bool handleOpenJpeg(const byte* buf, Image** image);
   bool handleSaveJpeg(Image* image, byte** buf);*/

/*
* helper function for freeing allocated image data
*/
void freeRows(byte** rowPtrs, size_t height);


/*
* compares the binary data with predefined constant format magic numbers
* was added as a help function because it deals easily with edge cases
* each format may have a different length of magic number bits and using
* this function saves the need from validating the buffer's length
*/
bool isFormatMatch(const byte* buf, const byte* format, int formatLen);

/*
* returns an enum representation of the buffer's format
* enum value 'None' is returned if it is not supported
*/
enum format isFormatSupported(const byte* buf);

/*
* verifies the requested save format in saveImage is supported (case-insensitive to *INPUT*)
* takes the assumption that the constant of the format is UPPERCASED, so each newly added
* format will need to follow this convention (define uppercased constant)
*/
bool formatCompIgnoreCase(const char* s1, const char* s2);

/*
* translates between the byte value of color-type in the png metadata
* and the corresponding color-type enum. the reason a conversion function
* was made instead of just setting the enum's values to match with the 
* color-type values is for cross-type compatibility, each type will have
* its own dictionary for translation of color-type
*/
enum colorType pngColorTypeDictionary(byte colorType);
