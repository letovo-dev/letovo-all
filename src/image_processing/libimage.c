#include "libimage.h"

int min(int a, int b) {
    return a < b ? a : b;
}

Image* decodeOneStep(const char* filename) {
    unsigned error;
    Image* img = (Image*)malloc(sizeof(Image));
    img->image = (unsigned char*)"";
    img->width = 0;
    img->height = 0;
    img->pngsize = 0;
    img->side = 0;

    error = lodepng_load_file(&img->image, &img->pngsize, filename);
    if(!error) error = lodepng_decode32(&img->image, &img->width, &img->height, img->image, img->pngsize);
    if(error) printf("error %u: %s\n", error, lodepng_error_text(error));
    img->side = min((int)img->width, (int)img->height);
    return img;
}

void encodeOneStep(const char* filename, const Image* img) {
    unsigned error = lodepng_encode32_file(filename, img->image, img->side, img->side);
    if(error) printf("error %u: %s\n", error, lodepng_error_text(error));
    free(img->image);
    free((void*)img);
}

Image* cut_to_square(Image* img) {
    if (img->width == img->height) return img;
    printf("%d, %d, %d\n", img->side * img->side, img->width, img->height);
    if (img->width > img->side) {
        for(int i = 0; i < img->side; i += 1) {
            memmove(
                img->image + i * img->side * PIXEL_SIZE,
                img->image + i * img->width * PIXEL_SIZE,
                img->side * PIXEL_SIZE
            );
        }
    }
    if(img->height > img->side) {
        img->image[img->side * img->side * PIXEL_SIZE] = '\0';
    }
    img->width = img->side; img->height = img->side;
    return img;
}

Image* cut_corners(Image* img, unsigned char blank) {
    printf("%d\n", img->side * img->side);
    int i;
    for(i = 0; i < img->side * img->side; i += 1) {
        if (
            0
            || i % img->side + (int)(i / img->side) < TOP_LEFT_CORNER(img->side)
            || i % img->side + (int)(i / img->side) >= BOTTOM_RIGHT_CORNER(img->side)
            || i % img->side - (int)(i / img->side) <= BOTTOM_LEFT_CORNER(img->side)
            || (int)(i / img->side) - i % img->side <= TOP_RIGHT_CORNER(img->side)
        ) {
            img->image[i * PIXEL_SIZE] = blank;
            img->image[i * PIXEL_SIZE + 1] = blank;
            img->image[i * PIXEL_SIZE + 2] = blank;
            img->image[i * PIXEL_SIZE + 3] = blank;
        }
    }
    for(; i < img->width * img->height; i += 1) {
        img->image[i * PIXEL_SIZE] = blank;
        img->image[i * PIXEL_SIZE + 1] = blank;
        img->image[i * PIXEL_SIZE + 2] = blank;
        img->image[i * PIXEL_SIZE + 3] = blank;
    }
    return img;
}

const unsigned char get_blank(const char* blank_file_path) {
    unsigned char* blank_img;
    size_t pngsize;
    unsigned error = lodepng_load_file(&blank_img, &pngsize, blank_file_path);
    if(error) printf("error %u: %s\n", error, lodepng_error_text(error));

    return blank_img[0];
}

void process_image(const char* filename) {
    unsigned char blank = get_blank("blank.png");

    encodeOneStep(
        filename, 
        cut_corners(
            cut_to_square(
                decodeOneStep(filename)
            )
            , blank
        )
    );
}