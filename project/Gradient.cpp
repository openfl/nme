#include "Gradient.h"
#ifdef WIN32
#include <windows.h>
#endif

#include <SDL_opengl.h>
#include <vector>

#include "nme.h"

#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif


#ifndef GL_CLAMP_TO_EDGE
  #define GL_CLAMP_TO_EDGE 0x812F
#endif

#ifndef HXCPP
typedef value *array_ptr;
#endif

// These should match Graphics.hx
enum GradFlags
{
   gfRadial  = 0x0001,
   gfRepeat  = 0x0002,
   gfReflect = 0x0004,
};



struct GradPoint
{
   void FromValue(const value &inVal)
   {
      int col = val_int(val_field(inVal,val_id("col")));
      mColour.r = (col>>16) & 0xff;
      mColour.g = (col>>8) & 0xff;
      mColour.b = (col) & 0xff;
      double a = val_number(val_field(inVal,val_id("alpha")));
      mColour.a = (a<0 ? 0 : a>=1.0 ? 255 : (int)(a*255) );

      mPos = val_int(val_field(inVal,val_id("ratio")));
   }

   ARGB   mColour;
   int    mPos;
};

typedef std::vector<GradPoint> GradPoints;

Gradient *CreateGradient(value inVal)
{
   if (val_is_null(inVal))
      return 0;

   if (val_is_null(val_field(inVal,val_id("points"))))
      return 0;

   return new Gradient( val_field(inVal,val_id("flags")),
                        val_field(inVal,val_id("points")),
                        val_field(inVal,val_id("matrix")),
                        val_field(inVal,val_id("focal")) );
}


/*
  The flash matrix transforms the "nominal gradient box",
    (-819.2,-819.2) ... (819.2,819.2).  The gradient values (0,0)...(1,1)
    are then "drawn" in this box.  We want the inverse of this.
    First we invert the transform, then we invert the +-819.2 -> 0..1 mapping.

  It is slightly different for the radial case.
*/

static void FlashMatrix2NME(const Matrix &inFlash, Matrix &outNME,bool inRadial)
{
   outNME = inFlash.Inverse();
   double fact = inRadial ? (1.0/819.2) : (1.0/1638.4);
   outNME.m00 *= fact;
   outNME.m01 *= fact;
   outNME.m10 *= fact;
   outNME.m11 *= fact;
   outNME.mtx *= fact;
   outNME.mty *= fact;
   if (!inRadial)
   {
      outNME.mtx += 0.5;
      outNME.mty += 0.5;
   }
}

Gradient::Gradient(value inFlags,value inHxPoints,value inMatrix,value inFocal)
{
   mFlags = (unsigned int)val_int(inFlags);

   FlashMatrix2NME(inMatrix,mOrigMatrix,mFlags & gfRadial);

   IdentityTransform();

   mTextureID = 0;
   mResizeID = 0;

   #ifdef HXCPP
   value inPoints = inHxPoints;
   int n =  inPoints->__length();
   #else
   value inPoints = val_field(inHxPoints,val_id("__a"));
   int n =  val_int( val_field(inHxPoints,val_id("length")));
   #endif

   array_ptr items = val_array_ptr(inPoints);

   GradPoints points(n);

   mRepeat = (mFlags & gfRepeat) != 0;

   mFX = val_number(inFocal);

   mUsesAlpha = false;
   for(int i=0;i<n;i++)
   {
      points[i].FromValue(items[i]);
      if (points[i].mColour.a<255)
         mUsesAlpha = true;
   }


   mColours.resize(256);
   if (n==0)
      memset(&mColours[0],0,256*sizeof(ARGB));
   else
   {
      int i;
      int last = points[0].mPos;
      if (last>255) last = 255;

      for(i=0;i<=last;i++)
         mColours[i] = points[0].mColour;
      for(int k=0;k<n-1;k++)
      {
         ARGB c0 = points[k].mColour;
         int p0 = points[k].mPos;
         int p1 = points[k+1].mPos;
         int diff = p1 - p0;
         if (diff>0)
         {
            if (p0<0) p0 = 0;
            if (p1>256) p1 = 256;
            int dr = points[k+1].mColour.r - c0.r;
            int dg = points[k+1].mColour.g - c0.g;
            int db = points[k+1].mColour.b - c0.b;
            int da = points[k+1].mColour.a - c0.a;
            for(i=p0;i<p1;i++)
            {
               mColours[i].r = c0.r + dr*(i-p0)/diff;
               mColours[i].g = c0.g + dg*(i-p0)/diff;
               mColours[i].b = c0.b + db*(i-p0)/diff;
               mColours[i].a = c0.a + da*(i-p0)/diff;
            }
         }
      }
      for(;i<256;i++)
         mColours[i] = points[n-1].mColour;
   }
}

#ifdef NME_OPENGL
bool Gradient::InitOpenGL()
{
   mResizeID = nme_resize_id;
   glGenTextures(1, &mTextureID);
   glBindTexture(GL_TEXTURE_1D, mTextureID);
   glTexImage1D(GL_TEXTURE_1D, 0, 4,  256, 0,
      GL_BGRA, GL_UNSIGNED_BYTE, &mColours[0] );
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);   
   glTexEnvi(GL_TEXTURE_1D, GL_TEXTURE_ENV_MODE, GL_REPLACE);   
   // TODO: reflect = double up?
   if (mFlags & gfRepeat)
      glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S,GL_REPEAT);
   else
      glTexParameterf(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);

   return true;
}
#endif

bool Gradient::Is2D()
{
   return (mFlags & gfRadial) != 0;
}

bool Gradient::IsFocal0()
{
   return mFX == 0.0;
}




int Gradient::MapHQ(int inX,int inY)
{
   return (int)(mTransMatrix.m00*inX + mTransMatrix.m01*inY + mTransMatrix.mtx*65536.0);
}

int Gradient::DGDX()
{
   return (int)(mTransMatrix.m00*65536.0);
}

int Gradient::DGDY()
{
   return (int)(mTransMatrix.m01*65536.0);
}


Gradient::~Gradient()
{
#ifdef NME_OPENGL
   if (mTextureID && mResizeID==nme_resize_id)
      glDeleteTextures(1,&mTextureID);
#endif
}


#ifdef NME_OPENGL
void Gradient::BeginOpenGL()
{
   if ( (mTextureID>0 && mResizeID==nme_resize_id)  || InitOpenGL())
   {
      glColor4f(1,1,1,1);
      glBindTexture(GL_TEXTURE_1D, mTextureID);
      glEnable(GL_TEXTURE_1D);
   }
}

void Gradient::OpenGLTexture(double inX,double inY)
{
   glTexCoord1d( mTransMatrix.m00*inX + mTransMatrix.m01*inY + mTransMatrix.mtx);
}

void Gradient::EndOpenGL()
{
   glDisable(GL_TEXTURE_1D);
}

#endif


