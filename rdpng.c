
#include "cdjpeg.h"   /* Common decls for cjpeg/djpeg applications */

#ifdef PNG_SUPPORTED

#include <png.h>      /* if this fails, you need to install libpng-devel */


typedef struct png_source_struct {
    struct cjpeg_source_struct pub;
    png_structp  png_ptr;
    png_infop    info_ptr;
    JDIMENSION   current_row;
} png_source_struct;


METHODDEF(void)
finish_input_png (j_compress_ptr cinfo, cjpeg_source_ptr sinfo);

METHODDEF(JDIMENSION)
get_pixel_rows_png (j_compress_ptr cinfo, cjpeg_source_ptr sinfo);

METHODDEF(void)
start_input_png (j_compress_ptr cinfo, cjpeg_source_ptr sinfo);


GLOBAL(cjpeg_source_ptr)
jinit_read_png(j_compress_ptr cinfo)
{
    png_source_struct *source = (*cinfo->mem->alloc_small)((j_common_ptr) cinfo, JPOOL_IMAGE, sizeof(png_source_struct));

    memset(source, 0, sizeof(*source));

    /* Fill in method ptrs, except get_pixel_rows which start_input sets */
    source->pub.start_input = start_input_png;
    source->pub.finish_input = finish_input_png;

    return &source->pub;
}

METHODDEF(void) error_input_png(png_structp png_ptr, png_const_charp msg) {
    j_compress_ptr cinfo = png_get_error_ptr(png_ptr);
    ERREXITS(cinfo, JERR_PNG_ERROR, msg);
}

METHODDEF(void)
start_input_png (j_compress_ptr cinfo, cjpeg_source_ptr sinfo)
{
    png_source_struct *source = (png_source_struct *)sinfo;

    source->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, cinfo, error_input_png, NULL);
    source->info_ptr = png_create_info_struct(source->png_ptr);

    if (!source->png_ptr || !source->info_ptr) {
        ERREXITS(cinfo, JERR_PNG_ERROR, "Can't create read/info_struct");
        return;
    }

    png_set_palette_to_rgb(source->png_ptr);
    png_set_expand_gray_1_2_4_to_8(source->png_ptr);
    png_set_strip_alpha(source->png_ptr);
    png_set_interlace_handling(source->png_ptr);

    png_init_io(source->png_ptr, source->pub.input_file);
    png_read_info(source->png_ptr, source->info_ptr);

    png_uint_32 width, height;
    int bit_depth, color_type;
    png_get_IHDR(source->png_ptr, source->info_ptr, &width, &height,
                 &bit_depth, &color_type, NULL, NULL, NULL);

    if (width > 65535 || height > 65535) {
        ERREXITS(cinfo, JERR_PNG_ERROR, "Image too large");
        return;
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        cinfo->in_color_space = JCS_GRAYSCALE;
        cinfo->input_components = 1;
    } else {
        cinfo->in_color_space = JCS_RGB;
        cinfo->input_components = 3;
    }

    if (bit_depth == 16) {
        png_set_strip_16(source->png_ptr);
    }

    cinfo->data_precision = 8;
    cinfo->image_width = width;
    cinfo->image_height = height;

    double gamma = 0.45455;
    if (!png_get_valid(source->png_ptr, source->info_ptr, PNG_INFO_sRGB)) {
        png_get_gAMA(source->png_ptr, source->info_ptr, &gamma);
    }
    cinfo->input_gamma = gamma;
    sinfo->get_pixel_rows = get_pixel_rows_png;

    source->pub.marker_list = NULL;
    png_bytep profile = NULL;
    png_charp unused1 = NULL;
    int unused2 = 0;
    png_uint_32 proflen = 0;
    if (png_get_iCCP(source->png_ptr, source->info_ptr, &unused1, &unused2, &profile, &proflen) && /* your libpng is out of date if you get a warning here */
        profile && proflen) {
        if (proflen < 65535-14) {
            size_t datalen = proflen + 14;
            JOCTET *dataptr = (*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE, datalen);
            memcpy(dataptr, "ICC_PROFILE\0\x01\x01", 14);
            memcpy(dataptr + 14, profile, proflen);
            struct jpeg_marker_struct *marker = (*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE, sizeof(struct jpeg_marker_struct));
            marker->next = NULL;
            marker->marker = JPEG_APP0+2;
            marker->original_length = 0;
            marker->data_length = datalen;
            marker->data = dataptr;
            source->pub.marker_list = marker;
        } else {
            WARNMS(cinfo, JERR_PNG_PROFILETOOLARGE);
        }
    }

    png_read_update_info(source->png_ptr, source->info_ptr);

    png_size_t rowbytes = png_get_rowbytes(source->png_ptr, source->info_ptr);

    source->pub.buffer = (*cinfo->mem->alloc_sarray)((j_common_ptr)cinfo, JPOOL_IMAGE, (JDIMENSION)rowbytes, 1);
    source->pub.buffer_height = 1;
}

METHODDEF(JDIMENSION)
get_pixel_rows_png (j_compress_ptr cinfo, cjpeg_source_ptr sinfo)
{
    png_source_struct *source = (png_source_struct *)sinfo;

    png_read_row(source->png_ptr, source->pub.buffer[0], NULL);
    return 1;
}

METHODDEF(void)
finish_input_png (j_compress_ptr cinfo, cjpeg_source_ptr sinfo)
{
    png_source_struct *source = (png_source_struct *)sinfo;

    png_read_end(source->png_ptr, source->info_ptr);
    png_destroy_read_struct(&source->png_ptr, &source->info_ptr, NULL);
}

#endif
