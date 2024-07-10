#pragma once
#include "seeker/logger.h"
#include <iostream>
#include <string>
#include <locale>
#include <codecvt>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <freetype/ftpfr.h>
#include <freetype/ftadvanc.h>
#include <opencv2/opencv.hpp>

#if _WIN32
#include <windows.h>
#else

#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#endif

#define MAX_GLYPHS 10000
using std::string;

typedef struct TGlyph_ {
	FT_UInt index = 0;         /* glyph index                  */
	FT_Vector pos;             /* glyph origin on the baseline */
	FT_Glyph image = nullptr;  /* glyph image                  */
} TGlyph, * PGlyph;

class freeTypeTool {
private:
	FT_Library pFTLib = NULL;
	FT_Face pFTFace = NULL;
	FT_Error error;
	PGlyph glyph = NULL;
	FT_GlyphSlot slot = NULL;
	FT_Vector pen;
	FT_Matrix matrix;
	FT_Int i, j, p, q = 0;
	FT_Int x_max, y_max = 0;

	FT_ULong word = 0;

	FT_UInt num_glyphs = 0;
	TGlyph glyphs[MAX_GLYPHS];

	FT_Glyph image = nullptr;

	//font parameter
	string backgroundColor = "#FFFFFF";
	string font_color;
	string font_family;
	int font_size = 0;
	float transparency = 0;
	int R, G, B;
	// line parameter
	int line_width;
	int line_height;
	int line_x = 0;
	int line_y = 0;
	string line_color;

	wchar_t* wszString = 0;
	int wcsLen = 0;
	uint8_t* rgba = nullptr;

	std::mutex writeLock;

    std::vector<int> textIndex;

	std::wstring s2ws(const std::string& str) {
		std::wstring ret;
		try {
			std::wstring_convert<std::codecvt_utf8<wchar_t>> wcv;
			ret = wcv.from_bytes(str);
		}
		catch (const std::exception& e) {
			E_LOG("string to wstring failed: {}", e.what());
		}
		return ret;
	}

	void get_rgbValue(string fontColor, int& R, int& G, int& B) {
		fontColor = "0x" + fontColor.substr(1, 6);
		int num = std::stoi(fontColor, NULL, 16);
		R = num >> 16 & 0xFF;
		G = num >> 8 & 0xFF;
		B = num & 0xFF;
	}

	void creatBackground(uint8_t* imgData, int width, int height) {
		get_rgbValue(backgroundColor, R, G, B);
		// creat rgba background
		for (int i = 0; i < width * height; i++) {
			int j = i * 4;
			imgData[j] = B;
			imgData[j + 1] = G;
			imgData[j + 2] = R;
			imgData[j + 3] = transparency;
		}
	}

	void drawLine(uint8_t* imgData, int width) {
		get_rgbValue(line_color, R, G, B);
		int end;
		for (int k = 0; k < line_height; k++) {
			if (line_x + line_width < width) {
				end = line_width + ((line_y + k) * width);
			}
			else {
				end = (line_y + k + 1) * width;
			}
			for (int i = line_x + ((line_y + k) * width); i < end; i++) {
				int j = i * 4;
				imgData[j] = B;
				imgData[j + 1] = G;
				imgData[j + 2] = R;
				imgData[j + 3] = 255;
			}
		}
	}

	int initFT() {
		// 1. init freetype
		error = FT_Init_FreeType(&pFTLib);
		if (error) {
			pFTLib = 0;
			D_LOG("There is some error when init library");
			return -1;
		}

		//2. load font
		error = FT_New_Face(pFTLib, font_family.c_str(), 0, &pFTFace);
		if (error) {
			D_LOG("Open font failed");
			return -1;
		}

		//3. font size
		error = FT_Set_Pixel_Sizes(pFTFace, 0, font_size);
		if (error) {
			D_LOG("Set font size failed");
			return -1;
		}

		double angle = (0.0 / 360) * 3.14159 * 2;
		matrix.xx = (FT_Fixed)(cos(angle) * 0x10000L);
		matrix.xy = (FT_Fixed)(-sin(angle) * 0x10000L);
		matrix.yx = (FT_Fixed)(sin(angle) * 0x10000L);
		matrix.yy = (FT_Fixed)(cos(angle) * 0x10000L);

		return 0;
	}

	//int loadGlyph(int width, int height, int fontSize, int linesize) {
	int loadGlyph(int width, int height, int fontSize, int rowSpacing, std::vector<int>& v, bool heightLimit = true) {
		int rows = 0;
		slot = pFTFace->glyph;
		glyph = glyphs;

//		float i = font_size / 10 + 2;
		float i = font_size / 5 - 2;
		int pen_x = 0;
		float pen_y = (height - font_size + i) * 64;

		for (int n1 = 0; n1 < wcsLen; n1++) {
			memcpy(&word, wszString + n1, 2);
			glyph->index = FT_Get_Char_Index(pFTFace, word);
			if (error) {
				E_LOG("FT_Get_Char_Index error!");
			}
			glyph->pos.x = pen_x;
			glyph->pos.y = pen_y;
			D_LOG("font num: {}: {}, x: {}  y: {}", n1, glyph->index, pen_x, pen_y);

			error = FT_Load_Glyph(pFTFace, glyph->index, FT_LOAD_DEFAULT);
			if (error) {
				D_LOG("FT_Load_Glyph error");
				continue;
			}

			error = FT_Get_Glyph(pFTFace->glyph, &glyph->image);
			if (error) {
				D_LOG("FT_Get_Glyph error");
				continue;
			}
			if ((glyph->pos.x + slot->advance.x) > width * 64) {
				pen_x = 0;
				pen_y = pen_y - rowSpacing * 64;

				// Height limit
				if (pen_y < 0 && heightLimit) {
					break;
				}
				glyph->pos.x = pen_x;
				glyph->pos.y = pen_y;
				D_LOG("new line : {},{}", glyph->pos.x, glyph->pos.y);
				v.push_back(n1 - 1);
				rows += 1;
			}
			FT_Glyph_Transform(glyph->image, &matrix, &glyph->pos);

			pen_x += slot->advance.x;
			pen_y += slot->advance.y;
			glyph++;
		}
		num_glyphs = glyph - glyphs;
		D_LOG("num_glyphs : {}", num_glyphs);
		v.push_back((int)num_glyphs - 1);
		return rows;
	}

    //new API
	void writeGlyph(uint8_t* imgData, cv::Scalar fontColor1, int width, int height, int index = -1, cv::Scalar fontColor2 = 0) {
		cv::Scalar fontColor;
        for (int n = 0; n < num_glyphs; n++) {
			/* create a copy of the original glyph */
            if(index == -1){
                fontColor = fontColor1;
            }
            else{
                if(n < index){
                    fontColor = fontColor1;
                }
                else{
                    fontColor = fontColor2;
                }
            }
			error = FT_Glyph_Copy(glyphs[n].image, &image);
			if (error) {
				D_LOG("FT_Glyph_Copy is fail");
				continue;
			}

			error = FT_Glyph_To_Bitmap(&image, FT_RENDER_MODE_NORMAL, 0, 1);
			if (!error) {
				FT_BitmapGlyph bit = (FT_BitmapGlyph)image;
				//bitmap
				FT_Bitmap& bitmap = bit->bitmap;
				auto x = bit->left;
				auto y = height - bit->top;
				x_max = x + bitmap.width;
				y_max = y + bitmap.rows;
				for (int j = y, q = 0; j < y_max; ++j, q++) {
					//rgba = imgData + j * width * 4;
					rgba = imgData + (j * width * 4);
					for (int i = x, p = 0; i < x_max; ++i, p++) {
						if (i < 0 || j < 0 || i >= width || j >= height) {
							continue;
						}
						if (bitmap.buffer[q * bitmap.width + p] != 0) {
							rgba[i * 4] = fontColor[0];
							rgba[i * 4 + 1] = fontColor[1];
							rgba[i * 4 + 2] = fontColor[2];
							rgba[i * 4 + 3] = fontColor[3];
						}
					}
				}
			}
			else {
				D_LOG("drew the {} fail", n);
			}
			FT_Done_Glyph(image);
			image = NULL;
		}
		if (num_glyphs > 0) {
			for (int i = 0; i < num_glyphs; i++) {
				if (glyphs[i].image) {
					FT_Done_Glyph(glyphs[i].image);
					glyphs[i].image = NULL;
				}
			}
			num_glyphs = 0;
		}
		if (pFTFace) {
			FT_Done_Face(pFTFace);
			pFTFace = NULL;
		}
		if (pFTLib) {
			FT_Done_FreeType(pFTLib);
			pFTLib = NULL;

		}
	}

    //old API
    void writeGlyph(uint8_t* imgData, int width, int height, int fontOpacity) {
        for (int n = 0; n < num_glyphs; n++) {
            /* create a copy of the original glyph */
            error = FT_Glyph_Copy(glyphs[n].image, &image);
            if (error) {
                D_LOG("FT_Glyph_Copy is fail");
                continue;
            }

            error = FT_Glyph_To_Bitmap(&image, FT_RENDER_MODE_NORMAL, 0, 1);
            if (!error) {
                FT_BitmapGlyph bit = (FT_BitmapGlyph)image;
                //bitmap
                FT_Bitmap& bitmap = bit->bitmap;
                auto x = bit->left;
                auto y = height - bit->top;
                x_max = x + bitmap.width;
                y_max = y + bitmap.rows;
                for (int j = y, q = 0; j < y_max; ++j, q++) {
                    //rgba = imgData + j * width * 4;
                    rgba = imgData + (j * width * 4);
                    for (int i = x, p = 0; i < x_max; ++i, p++) {
                        if (i < 0 || j < 0 || i >= width || j >= height) {
                            continue;
                        }
                        if (bitmap.buffer[q * bitmap.width + p] != 0) {
                            rgba[i * 4] = B;
                            rgba[i * 4 + 1] = G;
                            rgba[i * 4 + 2] = R;
                            rgba[i * 4 + 3] = fontOpacity;
                        }
                    }
                }
            }
            else {
                D_LOG("drew the {} fail", n);
            }
            FT_Done_Glyph(image);
            image = NULL;
        }
        if (num_glyphs > 0) {
            for (int i = 0; i < num_glyphs; i++) {
                if (glyphs[i].image) {
                    FT_Done_Glyph(glyphs[i].image);
                    glyphs[i].image = NULL;
                }
            }
            num_glyphs = 0;
        }
        if (pFTFace) {
            FT_Done_Face(pFTFace);
            pFTFace = NULL;
        }
        if (pFTLib) {
            FT_Done_FreeType(pFTLib);
            pFTLib = NULL;

        }
    }

public:
	freeTypeTool() {};

	~freeTypeTool(){
        if(!textIndex.empty()){
            textIndex.clear();
            std::vector<int>().swap(textIndex);
        }
        if(image){
            FT_Done_Glyph(image);
            image = NULL;
        }
		if (num_glyphs > 0) {
			for (int i = 0; i < num_glyphs; i++) {
				if (glyphs[i].image) {
					FT_Done_Glyph(glyphs[i].image);
                    glyphs[i].image = NULL;
				}
			}
		}
		if (pFTFace) {
			FT_Done_Face(pFTFace);
			pFTFace = NULL;
		}
		if (pFTLib) {
			FT_Done_FreeType(pFTLib);
			pFTLib = NULL;

		}
	}

    // new API + 0807
	int draw(cv::Mat bgraMat, cv::Scalar fontColor, int rowSpacing, string text, bool line = false, int index = -1, cv::Scalar fontColor2 = 0) {
		int ret = initFT();
		if (ret < 0) {
			D_LOG("Init freetype error");
			return -1;
		}
#if _WIN32
		const char* zimu = text.c_str();
		wcsLen = ::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), NULL, 0);
		wszString = new wchar_t[wcsLen + 1];
		::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), wszString, wcsLen);
		wszString[wcsLen] = '\0';
#else
		auto wstring = s2ws(text);
		wcsLen = wstring.size();
		wszString = const_cast<wchar_t*>(wstring.c_str());
#endif
		int width = bgraMat.cols;
		int height = bgraMat.rows;

		std::vector<int> v;
		loadGlyph(width, height, font_size, rowSpacing, v);
        std::vector<int>().swap(v);
		writeGlyph(bgraMat.data, fontColor, width, height, index, fontColor2);

		if (line) {
			drawLine(bgraMat.data, width);
		}

		return 0;
	}

    // old API
    int draw(cv::Mat bgraMat, int rowSpacing, string text, int fontOpacity = 255, bool line = false) {
        bool heightLimit = true;
        int ret = initFT();
        if (ret < 0) {
            D_LOG("Init freetype error");
            return -1;
        }

#if _WIN32
        const char* zimu = text.c_str();
		wcsLen = ::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), NULL, 0);
		wszString = new wchar_t[wcsLen + 1];
		::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), wszString, wcsLen);
		wszString[wcsLen] = '\0';
		std::cout << wszString << std::endl;
#else
        auto wstring = s2ws(text);
        wcsLen = wstring.size();
        wszString = const_cast<wchar_t*>(wstring.c_str());
#endif
        transparency = 0;
        int width = bgraMat.cols;
        int height = bgraMat.rows;
        creatBackground(bgraMat.data, width, height);


        get_rgbValue(font_color, R, G, B);
        std::vector<int> v;
        loadGlyph(width, height, font_size, rowSpacing, v);
        std::vector<int>().swap(v);
        writeGlyph(bgraMat.data, width, height, fontOpacity);

        if (line) {
            drawLine(bgraMat.data, width);
        }

        return 0;
    }

    // new API
	void setFont(int fontSize, string fontFamily) {
		//font_color = fontColor;
		font_size = fontSize;
		font_family = fontFamily;
	}
    // old API
    void setFont(int fontSize, string fontColor, string fontFamily) {
        font_color = fontColor;
        font_size = fontSize;
        font_family = fontFamily;
    }

	void setDividLine(int lineWidth, int lineHeight, string lineColor = "#FFFFFF", int lineY = 0, int lineX = 0) {
		line_width = lineWidth;
		line_height = lineHeight;
		line_x = lineX;
		line_y = lineY;
		line_color = lineColor;

	}

    // old API
	int rowInformation(int fontSize, string text, string fontFamily, int width, std::vector<int>& v) {
		bool heightLimit = false;
		font_size = fontSize;
		font_family = fontFamily;
		int ret = initFT();
		if (ret < 0) {
			D_LOG("Init freetype error");
		}
#if _WIN32
		const char* zimu = text.c_str();
		wcsLen = ::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), NULL, 0);
		wszString = new wchar_t[wcsLen + 1];
		::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), wszString, wcsLen);
		wszString[wcsLen] = '\0';
#else
		auto wstring = s2ws(text);
		wcsLen = wstring.size();
		wszString = const_cast<wchar_t*>(wstring.c_str());
#endif
		int len = loadGlyph(width, 100, font_size, 46, v, heightLimit);

        if (num_glyphs > 0) {
            for (int i = 0; i < num_glyphs; i++) {
                if (glyphs[i].image) {
                    FT_Done_Glyph(glyphs[i].image);
                    glyphs[i].image = NULL;
                }
            }
            num_glyphs = 0;
        }
        if (pFTFace) {
            FT_Done_Face(pFTFace);
            pFTFace = NULL;
        }
        if (pFTLib) {
            FT_Done_FreeType(pFTLib);
            pFTLib = NULL;

        }

		return len;
	}
    // new API
    int rowInformation(int fontSize, string text, string fontFamily, int width) {
        if(!textIndex.empty()){
            textIndex.clear();
        }
        bool heightLimit = false;
        font_size = fontSize;
        font_family = fontFamily;
        int ret = initFT();
        if (ret < 0) {
            D_LOG("Init freetype error");
        }
#if _WIN32
        const char* zimu = text.c_str();
		wcsLen = ::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), NULL, 0);
		wszString = new wchar_t[wcsLen + 1];
		::MultiByteToWideChar(CP_ACP, NULL, zimu, strlen(zimu), wszString, wcsLen);
		wszString[wcsLen] = '\0';
#else
        auto wstring = s2ws(text);
        wcsLen = wstring.size();
        wszString = const_cast<wchar_t*>(wstring.c_str());
#endif
        int len = loadGlyph(width, 100, font_size, 46, textIndex, heightLimit);

        if (num_glyphs > 0) {
            for (int i = 0; i < num_glyphs; i++) {
                if (glyphs[i].image) {
                    FT_Done_Glyph(glyphs[i].image);
                    glyphs[i].image = NULL;
                }
            }
            num_glyphs = 0;
        }
        if (pFTFace) {
            FT_Done_Face(pFTFace);
            pFTFace = NULL;
        }
        if (pFTLib) {
            FT_Done_FreeType(pFTLib);
            pFTLib = NULL;

        }

        return len;
    }

    std::vector<int> getIndex() const{
        return textIndex;
    }
};