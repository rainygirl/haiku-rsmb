// PPM avoids depending on optional image translators in minimal ARM64 images.
#include <Application.h>
#include <Bitmap.h>
#include <Screen.h>
#include <cstdio>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 BApplication app("application/x-vnd.R-SMB-ScreenCapture");
 BScreen screen;BBitmap* bitmap=nullptr;
 if(screen.GetBitmap(&bitmap)!=B_OK)return 3;
 if(bitmap->ColorSpace()!=B_RGB32&&bitmap->ColorSpace()!=B_RGBA32){delete bitmap;return 4;}
 FILE* file=fopen(argv[1],"wb");if(!file){delete bitmap;return 5;}
 int w=bitmap->Bounds().IntegerWidth()+1,h=bitmap->Bounds().IntegerHeight()+1;
 fprintf(file,"P6\n%d %d\n255\n",w,h);
 for(int y=0;y<h;y++){auto row=(const unsigned char*)bitmap->Bits()+y*bitmap->BytesPerRow();for(int x=0;x<w;x++){unsigned char rgb[3]={row[x*4+2],row[x*4+1],row[x*4]};if(fwrite(rgb,1,3,file)!=3){fclose(file);delete bitmap;return 6;}}}
 bool ok=fclose(file)==0;delete bitmap;printf("Captured %d x %d\n",w,h);return !ok;
}
