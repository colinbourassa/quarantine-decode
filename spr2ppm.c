/**
 * Extracts sprites from the .SPR files packaged with
 * the 1994 game 'Quarantine' (Gametek / Imagexcel).
 * Depends on the companion .IMG file that contains
 * color palette information.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define MODEX_PLANES 4
#define BYTES_PER_LINE 32
#define PALETTE_DATA_OFFSET 0xD
#define PALETTE_SIZE_COLORS 256
#define PALETTE_SIZE_BYTES 768 // 3 bytes per color (R,G,B) * 256 colors
#define MAX_FILENAME_LEN 32

// Simple repeating byte pair found in the header of the SPR files,
// with each pair describing the width/height of each sprite
typedef struct
{
  uint8_t width;
  uint8_t height;
} width_height_pair;

// Repeating RGB color triplet found in the palette data
typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} palette_entry;

void linearize_planar_data(uint8_t* planar_data, uint8_t* linear_data, uint_fast16_t pixelcount);
bool decode_spr(const char* filename, palette_entry* palette);
bool read_palette(const char* filename, uint8_t* palette_data);
bool write_ppm(const char* filename_base,
               uint8_t sprite_index,
               palette_entry* palette,
               uint8_t* data,
               uint8_t width,
               uint8_t height);

/**
 *  Processes a single SPR package of sprite data and a single file containing
 *  palette data, and creates one Netpbm (.ppm) image file per sprite.
 */
int main (int argc, char** argv)
{
  int status = 0;

  if (argc < 3)
  {
    printf("Usage: %s <palette_file> <spr_file>\n", argv[0]);
    return status;
  }

  uint8_t palette_data[PALETTE_SIZE_BYTES];

  printf("Reading palette from %s and sprites from %s...\n", argv[1], argv[2]);

  if (read_palette(argv[1], palette_data))
  {
    status = decode_spr(argv[2], (palette_entry*)palette_data) ? 0 : -2;
  }
  else
  {
    status = -1;
  }

  return status;
}

/**
 * Re-linearizes pixel data that had been separated into four planes
 * for display in VGA Mode X. This was unnecessary for the .SPR data
 * that was tested; the pixel data was already linear.
 */
void linearize_planar_data(uint8_t* planar_data, uint8_t* linear_data, uint_fast16_t pixelcount)
{
  const uint_fast16_t pixels_per_plane = pixelcount / MODEX_PLANES;

  for (uint_fast8_t modex_plane = 0; modex_plane < MODEX_PLANES; ++modex_plane)
  {
    for (uint_fast16_t pixel_index = 0; pixel_index < pixels_per_plane; ++pixel_index)
    {
      const uint_fast16_t planar_index = (modex_plane * pixels_per_plane) + pixel_index;
      const uint_fast16_t linear_index = (MODEX_PLANES * pixel_index) + modex_plane;
      linear_data[linear_index] = planar_data[planar_index];
    }
  }
}

/**
 * Reads the RGB color data from the palette section of the file with the
 * provided name, and stores it in the array pointed to by palette_data. This
 * array is assumed to have a capacity of at least 768 bytes (3 bytes per color).
 */
bool read_palette(const char* filename, uint8_t* palette_data)
{
  bool status = false;
  int fd = open(filename, O_RDONLY);

  if (fd > 0)
  {
    if (lseek(fd, PALETTE_DATA_OFFSET, SEEK_SET) == PALETTE_DATA_OFFSET)
    {
      if (read(fd, palette_data, PALETTE_SIZE_BYTES) == PALETTE_SIZE_BYTES)
      {
        status = true;
      }
      else
      {
        fprintf(stderr, "Error: unable to read %d bytes from offset 0x%02X in '%s'.\n",
                PALETTE_SIZE_BYTES, PALETTE_DATA_OFFSET, filename);
      }
    }
    else
    {
      fprintf(stderr, "Error: unable to seek to offset 0x%02X in '%s'.\n", PALETTE_DATA_OFFSET, filename);
    }

    close(fd);
  }
  else
  {
    fprintf(stderr, "Error: failed to open '%s'.\n", filename);
  }

  return status;
}

/**
 * Reads pixel data for each sprite in an SPR file, combines it with the
 * previously read palette data, and writes a series of .ppm Netpbm pixmaps
 * that each contain a single image.
 */
bool decode_spr(const char* filename, palette_entry* pal_data)
{
  bool status = true;
  width_height_pair* width_height_data = 0;
  uint8_t* pixel_data = 0;
  uint8_t num_sprites = 0;

  int fd = open(filename, O_RDONLY);

  if (fd > 0)
  {
    if (read(fd, &num_sprites, sizeof(num_sprites)) == sizeof(num_sprites))
    {
      printf("Number of sprites in file: %d\n", num_sprites);

      // allocate space for the width/height byte pairs
      const uint16_t header_size = num_sprites * sizeof(width_height_pair);
      width_height_data = malloc(header_size);

      if (width_height_data)
      {
        if (read(fd, width_height_data, header_size) == header_size)
        {
          // for each sprite/texture in the SPR file
          for (uint8_t sprite_index = 0; sprite_index < num_sprites; ++sprite_index)
          {
            // read data only for sprites with a nonzero number of pixels
            if (width_height_data[sprite_index].width && width_height_data[sprite_index].height)
            {
              const uint_fast16_t pixel_count =
                width_height_data[sprite_index].width * width_height_data[sprite_index].height;

              pixel_data = malloc(pixel_count);

              // if we were able to reserve space for buffering the pixel data
              if (pixel_data)
              {
                // read the pixel data from the file for this sprite
                if (read(fd, pixel_data, pixel_count) == pixel_count)
                {
                  if (!write_ppm(filename,
                                 sprite_index,
                                 pal_data,
                                 pixel_data,
                                 width_height_data[sprite_index].width,
                                 width_height_data[sprite_index].height))
                  {
                    status = false;
                  }
                }
                else
                {
                  fprintf(stderr, "Error: failed to read %lu bytes of pixel data from '%s'.\n",
                          pixel_count, filename);
                  status = false;
                }

                free(pixel_data);
              }
              else
              {
                fprintf(stderr, "Error: failed to allocate %lu bytes for sprite at index %d.",
                        pixel_count, sprite_index);
                status = false;
              }
            }
          }
        }
        else
        {
          fprintf(stderr, "Error: failed to read %d bytes of header data from '%s'.\n",
                  header_size, filename);
          status = false;
        }

        free(width_height_data);
      }
      else
      {
        fprintf(stderr, "Error: failed to allocate %d bytes to buffer header data.\n", header_size);
        status = false;
      }
    }
    else
    {
      fprintf(stderr, "Error: failed to read sprite count field in header of '%s'\n", filename);
      status = false;
    }
  }
  else
  {
    fprintf(stderr, "Error: failed to open '%s'.\n", filename);
    status = false;
  }

  return status;
}

/**
 * Writes a P3-style netpbm (portable pixel map) image.
 */
bool write_ppm(const char* filename_base,
               uint8_t sprite_index,
               palette_entry* palette,
               uint8_t* data,
               uint8_t width,
               uint8_t height)
{
  bool status = false;
  char filename[MAX_FILENAME_LEN];

  snprintf(filename, MAX_FILENAME_LEN - 1, "%s_%03d.ppm", filename_base, sprite_index);

  FILE* fd = fopen(filename, "w");
  const uint_fast16_t pixel_count = width * height;
  uint8_t pal_index = 0;

  if (fd > 0)
  {
    fprintf(fd, "P3\n%d %d\n255", width, height);

    for (uint_fast16_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index)
    {
      if (pixel_index % 4 == 0)
      {
        fprintf(fd, "\n");
      }
      pal_index = data[pixel_index];
      fprintf(fd, "%03d %03d %03d   ", palette[pal_index].r,
                                       palette[pal_index].g,
                                       palette[pal_index].b);
    }

    fclose(fd);
    status = true;
  }
  else
  {
    fprintf(stderr, "Error: unable to open '%s' for writing.\n", filename);
  }

  return status;
}

