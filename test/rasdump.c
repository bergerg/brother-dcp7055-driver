/* Dump raster pages as: "<height> <width bytes>\n" + raw pixels. */
#include <cups/raster.h>
#include <stdio.h>
int main(void){cups_raster_t*r=cupsRasterOpen(0,CUPS_RASTER_READ);cups_page_header2_t h;static unsigned char l[100000];
while(cupsRasterReadHeader2(r,&h)){printf("%u %u\n",h.cupsHeight,h.cupsBytesPerLine);fprintf(stderr,"%s %ux%u dpi=%u bpp=%u cs=%u copies=%u\n",h.cupsPageSizeName,h.cupsWidth,h.cupsHeight,h.HWResolution[0],h.cupsBitsPerPixel,h.cupsColorSpace,h.NumCopies);
for(unsigned y=0;y<h.cupsHeight;y++){cupsRasterReadPixels(r,l,h.cupsBytesPerLine);fwrite(l,1,h.cupsBytesPerLine,stdout);}}return 0;}
