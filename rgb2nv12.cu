
// BlockDim = 16x16
//GridDim = w/16*h/16
extern "C" __global__ void RGB2NV12( unsigned char *in_rgb, unsigned char *nv12,
        int rgb_width, int rgb_height, int rgb_pitch, int nv12_pitch )
{
    unsigned char *rgb1,*rgb2,*rgb3,*rgb4, r,g,b,u,v;
    unsigned char *pYDst, *pUVDst;
    int x,y,uv_y;
    x = blockIdx.x*blockDim.x+threadIdx.x;
    y = blockIdx.y*blockDim.y+threadIdx.y;//Y 
    uv_y = y + (rgb_height<<1);// UV 

    if ((x < rgb_width) && (y < rgb_height))
    {
        rgb1 = in_rgb + (y << 1) * rgb_pitch + (x << 1) * 3;
        rgb2 = in_rgb + (y << 1) * rgb_pitch + ((x << 1) + 1) * 3;
        rgb3 = rgb1 + rgb_pitch;
        rgb4 = rgb2 + rgb_pitch;

        pYDst = nv12 + (y << 1)*nv12_pitch;
        u = -0.09991f * rgb1[0] - 0.33609f * rgb1[1] + 0.436f * rgb1[2] + 128;
        v = 0.614f * rgb1[0] - 0.55861f * rgb1[1] - 0.05639f * rgb1[2] + 128;
        pYDst[x << 1] = (rgb1[0]+ rgb1[1] + rgb1[2])/3.492f + 104.339f - 0.446f*u - 0.224f*v;//Y 

        u = -0.09991f * rgb2[0] - 0.33609f * rgb2[1] + 0.436f * rgb2[2] + 128;
        v = 0.614f*rgb2[0]  - 0.55861f*rgb2[1]  - 0.05639f*rgb2[2]  + 128;
        pYDst[(x << 1) + 1] = (rgb2[0] + rgb2[1] +rgb2[2])/3.492f + 104.339f - 0.446f*u - 0.224f*v;//Y

        pYDst = nv12 + ((y << 1) + 1)*nv12_pitch;
        u = -0.09991f * rgb3[0]- 0.33609f * rgb3[1] + 0.436f * rgb3[2] + 128;
        v = 0.614f * rgb3[0]  - 0.55861f * rgb3[1] - 0.05639f * rgb3[2] + 128;
        pYDst[x << 1] = (rgb3[0]+ rgb3[1] + rgb3[2])/3.492f + 104.339f - 0.446f*u - 0.224f*v;//Y

        u = -0.09991f * rgb4[0] - 0.33609f * rgb4[1] + 0.436f * rgb4[2] + 128;
        v = 0.614f * rgb4[0] - 0.55861f * rgb4[1] - 0.05639f * rgb4[2] + 128;
        pYDst[(x << 1) + 1] = (rgb4[0]+ rgb4[1] + rgb4[2])/3.492f + 104.339f - 0.446f*u - 0.224f*v;//Y

        r = (rgb1[0] + rgb2[0] + rgb3[0] + rgb4[0])/4;
        g = (rgb1[1] + rgb2[1] + rgb3[1] + rgb4[1])/4;
        b = (rgb1[2] + rgb2[2] + rgb3[2] + rgb4[2])/4;
        pUVDst = nv12 + uv_y*nv12_pitch;
        pUVDst[x << 1] =  -0.09991f*r - 0.33609f*g + 0.436f*b + 128;//U
        pUVDst[(x << 1) + 1] = 0.614f*r - 0.55861f*g - 0.05639f*b + 128;//V

    }
}

