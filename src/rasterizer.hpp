#pragma once

#include<iostream>
#include<Eigen/Dense>
#include<Eigen/LU>	//for .inverse().  Probably not needed
#include<vector>
#include<array>
#include<memory>
#include<functional>

namespace SoftwareRasterizer
{
//This is the framebuffer class.  It's a part of namespace SoftwareRasterizer because the SoftwareRasterizer needs to have a well-defined image class to render to.
//It is templated because the output type need not be only colors, could contain anything (like a stencil buffer or depth buffer or gbuffer for deferred rendering)
template<class PixelType>
class Framebuffer
{
protected:
	std::vector<PixelType> data;
public:
	const std::size_t width;
	const std::size_t height;
	//constructor initializes the array
	Framebuffer(std::size_t w,std::size_t h):
		data(w*h),
		width(w),height(h)
	{}
	//2D pixel access
	PixelType& operator()(std::size_t x,std::size_t y)
	{
		return data[y*width+x];
	}
	//const version
	const PixelType& operator()(std::size_t x,std::size_t y) const
	{
		return data[y*width+x];
	}
	void clear(const PixelType& pt=PixelType())
	{
		std::fill(data.begin(),data.end(),pt);
	}
};

//This function runs the vertex shader on all the vertices, producing the varyings that will be interpolated by the rasterizer.
//VertexVsIn can be anything, VertexVsOut MUST have a position() method that returns a 4D vector, and it must have an overloaded *= and += operator for the interpolation
//The right way to think of VertexVsOut is that it is the class you write containing the varying outputs from the vertex shader.
template<class VertexVsIn,class VertexVsOut,class VertShader>
void RunVertexShader(const VertexVsIn* pBegin,const VertexVsIn* pEnd,VertexVsOut* pOutput,
	VertShader vertexShader)
{
	std::size_t n=pEnd-pBegin;
	#pragma omp parallel for
	for(std::size_t i=0;i<n;i++)
	{
		pOutput[i]=vertexShader(pBegin[i]);
	}
}
struct BarycentricTransform
{
private:
	Eigen::Vector2f offset;
	Eigen::Matrix2f Ti;
public:
	BarycentricTransform(const Eigen::Vector2f& s1,const Eigen::Vector2f& s2,const Eigen::Vector2f& s3):
		offset(s3)
	{
		Eigen::Matrix2f T;
		T << (s1-s3),(s2-s3);
		Ti=T.inverse();
	}
	Eigen::Vector3f operator()(const Eigen::Vector2f& v) const
	{
		Eigen::Vector2f b;
		b=Ti*(v-offset);
		return Eigen::Vector3f(b[0],b[1],1.0f-b[0]-b[1]);
	}
};
//This function takes in 3 varyings vertices from the fragment shader that make up a triangle,
//rasterizes the triangle and runs the fragment shader on each resulting pixel.
template<class Pixel,class VertexVsOut,class FragShader>
void RasterizeTriangle(Framebuffer<Pixel>& framebuffer,const std::array<VertexVsOut,3>& verts,FragShader fragmentShader)
{
	std::array<Eigen::Vector4f,3> points{{verts[0].position(),verts[1].position(),verts[2].position()}};
	//Do the perspective divide by w to get screen space coordinates.
	std::array<Eigen::Vector4f,3> screenPoints{{points[0]/points[0][3],points[1]/points[1][3],points[2]/points[2][3]}};
	auto ss1=screenPoints[0].head<2>().array(),
			ss2=screenPoints[1].head<2>().array(),
			ss3=screenPoints[2].head<2>().array();

	//calculate the bounding box of the triangle in screen space floating point.
	Eigen::Array2f topLeft=ss1.min(ss2).min(ss3);
	Eigen::Array2f bottomRight=ss1.max(ss2).max(ss3);
	Eigen::Array2i frameSize(framebuffer.width,framebuffer.height);	

	//convert bounding box to fixed point.
	//move bounding box from (-1.0,1.0)->(0,imgdim)
	Eigen::Array2i fixedTopLeft=((topLeft*0.5f+0.5f)*frameSize.cast<float>()).cast<int>();	
	Eigen::Array2i fixedBottomRight=((bottomRight*0.5f+0.5f)*frameSize.cast<float>()).cast<int>();
	fixedBottomRight+=1;	//add one pixel of coverage

	//clamp the bounding box to the framebuffer size if necessary (this is clipping.  Not quite how the GPU actually does it but same effect sorta).
	fixedTopLeft=fixedTopLeft.max(Eigen::Array2i(0,0));
	fixedBottomRight=fixedBottomRight.min(frameSize);
	
	BarycentricTransform barycentricTransform(ss1.matrix(),ss2.matrix(),ss3.matrix());

	//for all the pixels in the bounding box
	for(int y=fixedTopLeft[1];y<fixedBottomRight[1];y++)
	for(int x=fixedTopLeft[0];x<fixedBottomRight[0];x++)
	{
		Eigen::Vector2f ssc(x,y);
		ssc.array()/=frameSize.cast<float>();	//move pixel to relative coordinates
		ssc.array()-=0.5f;
		ssc.array()*=2.0f;

		//Compute barycentricTransform coordinates of the pixel center
		Eigen::Vector3f bary=barycentricTransform(ssc);
		
		//if the pixel has valid barycentricTransform coordinates, the pixel is in the triangle
		if((bary.array() < 1.0f).all() && (bary.array() > 0.0f).all())
		{
			float depth=bary[0]*screenPoints[0][2]+bary[1]*screenPoints[1][2]+bary[2]*screenPoints[2][2];
			//Reference the current pixel at that coordinate
			Pixel& candidatePixel=framebuffer(x,y);
			// if the interpolated depth passes the depth test
			if(candidatePixel.depth() < depth && depth < 1.0)
			{
				//interpolate varying parameters
				VertexVsOut v;
				for(int i=0;i<3;i++)
				{	
					VertexVsOut vt=verts[i];
					vt*=bary[i];
					v+=vt;
				}
				//call the fragment shader
				candidatePixel=fragmentShader(v);
				candidatePixel.depth()=depth; //write the depth buffer
			}
		}
	}
}


//This function rasterizes a set of triangles determined by an index buffer and a buffer of output verts.
template<class Pixel,class VertexVsOut,class FragShader>
void Rasterize(Framebuffer<Pixel>& framebuffer,const std::size_t* indexBufferBegin,
	const std::size_t* indexBufferEnd,const VertexVsOut* verts,
	FragShader fragmentShader)
{
	std::size_t n=indexBufferEnd-indexBufferBegin;
	#pragma omp parallel for
	for(std::size_t i=0;i<n;i+=3)
	{
		const std::size_t* ti=indexBufferBegin+i;
		std::array<VertexVsOut,3> tri{{verts[ti[0]],verts[ti[1]],verts[ti[2]]}};
		RasterizeTriangle(framebuffer,tri,fragmentShader);
	}
}

//This function does a draw call from an indexed buffer
template<class Pixel,class VertexVsOut,class VertexVsIn,class VertShader, class FragShader>
void draw(	Framebuffer<Pixel>& framebuffer,
		const VertexVsIn* vertexBufferBegin,const VertexVsIn* vertexBufferEnd,
		const std::size_t* indexBufferBegin,const std::size_t* indexBufferEnd,
		VertexVsOut* vCacheBegin,VertexVsOut* vCacheEnd,
		VertShader vertexShader,
		FragShader fragmentShader)
{
	std::unique_ptr<VertexVsOut[]> vertexCache;
	if(vCacheBegin==NULL || (vCacheEnd-vCacheBegin) != (vertexBufferEnd-vertexBufferBegin))
	{
		vCacheBegin=new VertexVsOut[(vertexBufferEnd-vertexBufferBegin)];
		vertexCache.reset(vCacheBegin);
	}
	RunVertexShader(vertexBufferBegin,vertexBufferEnd,vCacheBegin,vertexShader);
	Rasterize(framebuffer,indexBufferBegin,indexBufferEnd,vCacheBegin,fragmentShader);
}

}
