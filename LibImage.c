#include "libimage.h"

void readFromBuffer(png_structp png_ptr, png_bytep dataBuffer, png_size_t bytesToRead)
{
    png_voidp io_ptr = png_get_io_ptr(png_ptr);
    if (!io_ptr)
    {
        printf("failed to fetch png_io_ptr\n");
        return;
    }

    Buffer* inBuffer = (Buffer*)io_ptr;
    memcpy(dataBuffer, inBuffer->buf, bytesToRead);
    inBuffer->buf += bytesToRead;
}

void writeToBuffer(png_structp png_ptr, png_bytep dataBuffer, png_size_t bytesToRead)
{
    png_voidp io_ptr = png_get_io_ptr(png_ptr);
    if (!io_ptr)
    {
        printf("failed to fetch png_io_ptr\n");
        return;
    }

    Buffer* outBuffer = (Buffer*)io_ptr;

    if (!outBuffer->buf)
    {
        outBuffer->buf = (byte*)malloc(sizeof(byte) * bytesToRead);
        if (!outBuffer->buf)
        {
            printf("allocation failed for out-buffer\n");
            return;
        }
    }
    else
    {
        byte* newBuf = (byte*)realloc(outBuffer->buf, sizeof(byte) * (outBuffer->size + bytesToRead));
        if (!newBuf)
        {
            printf("realloc failed for out-buffer\n");
            free(outBuffer->buf);
            return;
        }
        outBuffer->buf = newBuf;
    }
    memcpy(outBuffer->buf + outBuffer->size, dataBuffer, bytesToRead);
    outBuffer->size += bytesToRead;
}

/*
* return suffices here because once you return prematurely from read/write procedures libpng will
* detect the missing bytes in a short while (be it a null ptr, missing magic numbers or bad crc)
* once an error is raised, the next block of execution will be the setjmp that's in open/save
*/

bool isFormatMatch(const byte* inBuffer, const byte* format, int formatLen)
{
    int i;
    for (i = 0; i < formatLen; ++i)
    {
        if (inBuffer[i] == '\0' || inBuffer[i] != format[i])
        {
            return false;
        }
    }
    return true;
}


bool formatCompIgnoreCase(const char* s1, const char* s2)
{
    int size1 = strlen(s1);
    if (strlen(s2) != size1)
    {
        return false;
    }
    int i;
    for (i = 0; i < size1; ++i)
    {
        if (toupper(s1[i]) != s2[i])
        {
            return false;
        }
    }
    return true;
}

enum format isFormatSupported(const byte* inBuffer)
{
    if (!inBuffer)
    {
        return NoneFormat;
    }

    if (isFormatMatch(inBuffer, jpeg, JPEG_L) || isFormatMatch(inBuffer, jpeg2, JPEG_L))
    {
        return Jpeg;
    }
    if (isFormatMatch(inBuffer, png, PNG_L))
    {
        return Png;
    }
    return NoneFormat;
}

enum colorType pngColorTypeDictionary(byte colorType)
{
    switch (colorType)
    {
    case 0:
        return GrayScale;
    case 2:
        return RGB;
    case 3:
        return PLTE;
    case 4:
        return GSA;
    case 6:
        return RGBA;
    default:
        return NoneType;
    }
}

bool openImage(byte* inBuffer, Image** image)
{
    enum format imageFormat = isFormatSupported(inBuffer);
    switch (imageFormat)
    {
    case Png:
        return handleOpenPng(inBuffer, image);
    case Jpeg:
        /*
         * return handleOpenJpeg(buf, image);
         * support for the format can be added easily in the future
         */
    default:
        return false;
    }
}

bool handleOpenPng(byte* inBuffer, Image** image)
{
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        printf("creation of png_structp failed\n");
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
        printf("creation of png_infop failed\n");
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        printf("error while reading header\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        return false;
    }

    Buffer pngReadBuffer = { inBuffer, 0 };
    png_set_read_fn(png_ptr, &pngReadBuffer, readFromBuffer); 
    png_set_sig_bytes(png_ptr, 0);

    png_read_info(png_ptr, info_ptr);

    size_t width, height;
    byte colorType, bitDepth;
    byte** rowPtrs;

    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    colorType = png_get_color_type(png_ptr, info_ptr);
    bitDepth = png_get_bit_depth(png_ptr, info_ptr);

    enum colorType colorTypeEnum = pngColorTypeDictionary(colorType);
    if (colorTypeEnum == NoneType)
    {
        printf("data integrity error, color-type is unknown\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        return false;
    }

    png_read_update_info(png_ptr, info_ptr);

    rowPtrs = (byte**)malloc(sizeof(byte*) * height);
    if (!rowPtrs) 
    {
        printf("allocation for binary image data failed\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        return false;
    }
    size_t i;
    for (i = 0; i < height; ++i)
    {
        rowPtrs[i] = (byte*)malloc(png_get_rowbytes(png_ptr, info_ptr));
        if (!rowPtrs[i])
        {
            printf("allocation for binary image data failed\n");
            freeRows(rowPtrs, i);
            png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
            return false;
        }
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        printf("failed reading the image\n");
        freeRows(rowPtrs,height);
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        return false;
    }

    png_read_image(png_ptr, rowPtrs);

    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);

    Image* img = (Image*)malloc(sizeof(Image));
    if (!img)
    {
        printf("image allocation failed\n");
        freeRows(rowPtrs, height);
        png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        return false;
    }
    img->height = height;
    img->width = width;
    img->bitDepth = bitDepth;
    img->colorTypeVal = colorType;
    img->colorTypeEnum = colorTypeEnum;
    img->rowPtrs = rowPtrs;

    *image = img;

    return true;
}

void freeRows(byte** rowPtrs, size_t height) 
{
    size_t i;
    for (i = 0; i < height; ++i)
    {
        free(rowPtrs[i]);
    }
    free(rowPtrs);
}

bool saveImage(Image* image, const char* format, byte** outBuffer)
{
    if (!image || !format)
    {
        return false;
    }

    if (formatCompIgnoreCase(format, PNG))
    {
        return handleSavePng(image, outBuffer);
    }
    if (formatCompIgnoreCase(format, JPEG))
    {
        /*
         * return handleSaveJpeg(image, retBuffer);
         * support for the format can be added easily in the future
         */
    }

    return false;
}

bool handleSavePng(Image* image, byte** outBuffer)
{
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        printf("creation of png_structp failed\n");
        return false;
    }
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        printf("creation of png_infop failed\n");
        png_destroy_write_struct(&png_ptr, NULL);
        return false;
    }
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        printf("failed setting header info\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }
    png_set_IHDR(png_ptr, info_ptr, image->width, image->height, image->bitDepth, image->colorTypeVal,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_set_compression_level(png_ptr, 1);
    png_set_filter(png_ptr, 0, PNG_ALL_FILTERS);
    png_set_rows(png_ptr, info_ptr, image->rowPtrs);

    byte* emptyBuffer = NULL;
    Buffer pngWriteBuffer = { emptyBuffer, 0 };
    png_set_write_fn(png_ptr, &pngWriteBuffer, writeToBuffer, NULL);

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        printf("failed writing image data\n");
        if (!pngWriteBuffer.buf)
        {
            free(outBuffer);
        }
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    freeRows(image->rowPtrs, image->height);
    free(image);    
    png_destroy_write_struct(&png_ptr, &info_ptr);

    *outBuffer = pngWriteBuffer.buf;
    return true;
}

bool averageImage(int avgDim, Image* image)
{
    if (image->colorTypeEnum == RGBA && image->bitDepth == 8)
    {
        return handleEightBitRgbaAveraging(image, avgDim);
    }
    return false;
}

bool handleEightBitRgbaAveraging(Image* image, int avgDim)
{
    size_t newHeight = image->height / avgDim;
    size_t newWidth = image->width / avgDim;
    int travDistance = avgDim - 1;
    byte** rows = image->rowPtrs;

    byte** newRows = createAvgImage(rows, newHeight, newWidth, avgDim);
    if (!newRows)
    {
        return false;
    }

    image->height = newHeight;
    image->width = newWidth;
    image->rowPtrs = newRows;

    return true;
}

bool paveImage(int numOfImgs, Image* image, Image*** imageChunks)
{   
    if (image->colorTypeEnum == RGBA && image->bitDepth == 8)
    {
        return handleEightBitRgbaPaving(image, numOfImgs, imageChunks);
    }
    return false;
}

bool handleEightBitRgbaPaving(Image* image, int numOfImgs, Image*** imageChunks)
{
    size_t newHeight = image->height / numOfImgs;
    size_t newWidth = image->width / numOfImgs;
    byte** rows = image->rowPtrs;
    int travDistance = numOfImgs - 1;
    int i, size = numOfImgs * numOfImgs;
    byte*** newRowsList = (byte***)malloc(sizeof(byte**) * size);
    if (!newRowsList)
    {
        printf("allocation for chunked image list failed\n");
        return false;
    }

    int outerX, outerY;
    size_t y, x;
    for (outerY = 0; outerY < numOfImgs; ++outerY)
    {
        for (outerX = 0; outerX < numOfImgs; ++outerX)
        {
            int chunkOffest = outerY * numOfImgs + outerX;
            byte** newRows = (byte**)malloc(sizeof(byte*) * newHeight);
            if (!newRows)
            {
                printf("allocation for chunked image list failed\n");
                for (i = 0; i < chunkOffest; ++i)
                {
                    freeRows(newRowsList[i], newHeight);
                }
                return false;
            }

            newRowsList[chunkOffest] = newRows;
            size_t y_offest = outerY * newHeight;
            size_t x_offest = outerX * newWidth * 4;
            for (y = 0; y < newHeight; ++y)
            {
                byte* row = rows[y + y_offest];
                byte* newRow = (byte*)malloc(sizeof(byte) * newWidth * 4);
                if (!newRow)
                {
                    printf("allocation for chunked image list failed\n");
                    freeRows(newRows, y);
                    int i;
                    for (i = 0; i < chunkOffest - 1; ++i)
                    {
                        freeRows(newRowsList[i], newHeight);
                    }
                    return false;
                }

                newRows[y] = newRow;
                for (x = 0; x < newWidth; ++x)
                {
                    unsigned int pos = x * 4;
                    newRow[pos] = row[pos + x_offest];
                    newRow[pos + 1] = row[pos + x_offest + 1];
                    newRow[pos + 2] = row[pos + x_offest + 2];
                    newRow[pos + 3] = row[pos + x_offest + 3];
                }
            }
        }
    }

    Image** images = (Image**)malloc(sizeof(Image*) * size);

    for (i = 0; i < size; ++i)
    {
        images[i] = (Image*)malloc(sizeof(Image));
        images[i]->height = newHeight;
        images[i]->width = newWidth;
        images[i]->bitDepth = image->bitDepth;
        images[i]->colorTypeVal = image->colorTypeVal;
        images[i]->colorTypeEnum = image->colorTypeEnum;
        images[i]->rowPtrs = newRowsList[i];
    }

    *imageChunks = images;

    return true;
}

byte** createAvgImage(byte** rows, size_t newHeight, size_t newWidth, int numOfImgs)
{
    byte** newRows = (byte**)malloc(sizeof(byte*) * newHeight);
    if (!newRows)
    {
        printf("failed to allocate memory for averaged image");
        return NULL;
    }

    size_t y, x;
    for (y = 0; y < newHeight; ++y)
    {
        byte* row = rows[y];
        byte* newRow = (byte*)malloc(sizeof(byte) * newWidth * 4);
        if (!newRow)
        {
            printf("failed to allocate memory for averaged image");
            freeRows(newRows, y);
            return NULL;
        }
        newRows[y] = newRow;
        for (x = 0; x < newWidth; ++x)
        {
            size_t pos = x * 4;
            int avgs[4] = { 0 };
            calcAverage(rows, numOfImgs, pos * numOfImgs, y * numOfImgs, avgs);
            newRow[pos++] = avgs[0];
            newRow[pos++] = avgs[1];
            newRow[pos++] = avgs[2];
            newRow[pos] = avgs[3];
        }
    }
    return newRows;
}

void calcAverage(byte** rows, int avgDim, size_t start_x, size_t start_y, int* avgs)
{
    int y = 0, x = 0;
    int sum[4] = { 0 };
    for (y = 0; y < avgDim; ++y)
    {
        byte* row = rows[y + start_y];
        for (x = 0; x < avgDim * 4; x += 4)
        {
            size_t pos = x + start_x;
            sum[0] += row[pos++];
            sum[1] += row[pos++];
            sum[2] += row[pos++];
            sum[3] += row[pos];
        }
    }
    int i, size = avgDim * avgDim;
    for (i = 0; i < 4; ++i)
    {
        avgs[i] = sum[i] / size;
    }
}

/*
* the library didn't use the file system as requested, however,
* writePngToFile and the main were made for QA purposes to make
* sure that the binary data represents the image faithfully
*
* there is no error handling because it was written solely
* for testing and not for internal/external usage in any way
*/

void writePngToFile(Image* image, char* fileName)
{
    FILE* fp = fopen(fileName, "wb");
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (setjmp(png_jmpbuf(png_ptr)))
        return;
    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, image->width, image->height,
        image->bitDepth, image->colorTypeVal, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);
    png_write_image(png_ptr, image->rowPtrs);
    png_write_end(png_ptr, NULL);
    fclose(fp);
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printf("for testing, input argument == imagename\n");
        return -1;
    }
    

    FILE* fp;
    byte* buf, * buf2, * buf3;
    long flen;

    fp = fopen(argv[1], "rb");
    fseek(fp, 0, SEEK_END);
    flen = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buf = (byte*)malloc((flen) * sizeof(byte));
    size_t bytes_read = fread(buf, sizeof(byte), flen, fp);
    fclose(fp);
    buf2 = buf3 = buf;

    Image* image, *image2, *image3, **images;
    if (openImage(buf, &image))
        printf("open success #1!\nsaving as test1\n\n");
    else 
    {
        printf("open error\n"); 
        return -1;
    }

    writePngToFile(image, "test1.png");

    if (saveImage(image, "pNg", &buf2))
        printf("save #1 to buffer success\n");
    else
    {
        printf("save #1 error\n");
        return -1;
    }
    if (openImage(buf2, &image2))
        printf("open success #2!\n");
    else
    {
        printf("open #2 error\n");
        return -1;
    }
    if (averageImage(4, image2))
        printf("average success\nsaving as test2\n\n");
    else
    {
        printf("average error\n");
        return -1;
    }
    writePngToFile(image2, "test2.png");
    
    if(saveImage(image2,"png", &buf3))
        printf("save #2 to buffer success\n");
    else
    {
        printf("save #2 error\n");
        return -1;
    }

    if (openImage(buf3, &image3))
        printf("open #3 success\n");

    int size = 4;
    if (paveImage(size, image3, &images))
        printf("pave success\nsaving chunks as tst1-%d\n",size*size);
    else
    {
        printf("paving error\n");
        return -1;
    }
   
    int i;
    for (i = 0; i < size*size; ++i)
    {
        char buf[10];
        sprintf(buf, "tst%d.png", i+1);
        writePngToFile(images[i], buf);
    }
    
    return 0;
}
