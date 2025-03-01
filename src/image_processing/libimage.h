#include "lodepng.h"
#include <stdio.h>
#include <stdlib.h>

#define PIXEL_SIZE 4
#define CORNER_SIZE 3.5
#define BLANK(img) img.data[0]

#define TOP_LEFT_CORNER(side) side / CORNER_SIZE
#define TOP_RIGHT_CORNER(side) side / CORNER_SIZE - side + 3
#define BOTTOM_LEFT_CORNER(side) side / CORNER_SIZE - side + 3
#define BOTTOM_RIGHT_CORNER(side) (side * 2 - side / CORNER_SIZE) - 1
#define INDEX(i, pixel_size) i * pixel_size

typedef struct {
    unsigned char* image;
    unsigned width;
    unsigned height;
    int side;
    size_t pngsize;
} Image;

int min(int a, int b);

Image* decodeOneStep(const char*);

void encodeOneStep(const char*, const Image*);

Image* cut_to_square(Image* img);

Image* cut_corners(Image*, unsigned char);

const unsigned char get_blank(const char*);

void process_image(const char*);