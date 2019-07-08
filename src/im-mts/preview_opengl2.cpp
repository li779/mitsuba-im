#include "shell.h"
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_opengl.h>

namespace impl {

	static void setupTexture(GLenum target) {
		glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
	}

	struct ImagePreview: ::ImagePreview{
		GLuint texture;
		int generation;
		int revision;

		volatile int currentGeneration = 0;

		void reset() override {
			++currentGeneration;
		}

		ImagePreview(int x, int y) {
			this->resX = x;
			this->resY = y;

			{
				this->generation = currentGeneration;
				this->revision = 0;
				glGenTextures(1, &texture);
			}
			this->previewImg = 0; // set after the first write
		}

		~ImagePreview() {
			glDeleteTextures(1, &texture);
		}

		void update(float const* data, int const volatile* dataRevision) override {
			int drev = *dataRevision;
			if (drev && (currentGeneration != generation || drev != revision)) {
				generation = currentGeneration;
				revision = drev;
				glBindTexture(GL_TEXTURE_2D, texture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, this->resX, this->resY, 0, GL_RGBA, GL_FLOAT, data);
				setupTexture(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
				this->previewImg = (intptr_t) texture;
			}
		}
	};

} // namespace

ImagePreview* ImagePreview::create(int x, int y) {
	return new impl::ImagePreview(x, y);
}
ImagePreview::~ImagePreview() = default;

namespace impl {

	struct StackedPreview: ::StackedPreview{
		std::vector<GLuint> textures;
		std::vector<unsigned long long> stamps;
		std::vector<double> samples;

		GLuint fbt, fbo = 0;
		PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
		PFNGLBLENDCOLORPROC glBlendColor = nullptr;

		PFNGLGENERATEMIPMAPPROC glGenMipmap;
		bool haveMips = false;

		volatile unsigned long long currentBasetime = 0;

		void nextGeneration() override {
			currentBasetime = 0;
		}
		void runGeneration(unsigned long long timestamp) override {
			assert(timestamp > 0);
			currentBasetime = timestamp;
		}

		bool upToDate(double const volatile dataSamples[], int maxN) const override {
			assert(maxN <= (int) dataSamples.size());
			for (int i = 0; i < maxN; ++i) {
				double dataSpp = dataSamples[i];
				if (dataSpp && (currentBasetime > (volatile unsigned long long&) stamps[i / workersPerTarget] || dataSpp != (volatile double&) samples[i]))
					return false;
			}
			return true;
		}
		bool ready(unsigned long long timestamp) const {
			bool hasData = false;
			int basetime = currentBasetime;
			if (basetime != 0) {
				for (int i = 0; i < (int) stamps.size(); ++i) {
					hasData |= (basetime <= (volatile unsigned long long&) stamps[i]);
				}
			}
			return hasData && timestamp >= basetime + this->readyMS;
		}

		StackedPreview(int x, int y, int maxN, int maxT) {
			this->resX = x;
			this->resY = y;
			this->workersPerTarget = maxN / maxT;
			assert(workersPerTarget >= 1 && workersPerTarget * maxT == maxN);

			{
				this->stamps.resize(maxT, 0);
				this->samples.resize(maxN, 0);
				if (maxT > 1) {
					this->textures.resize(maxT);
					glGenTextures(maxT, textures.data());
				}
			}

			{
				glGenTextures(1, &fbt);
				glBindTexture(GL_TEXTURE_2D, fbt);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, x, y, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
				setupTexture(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			bool needComposition = maxT > 1;
			if (needComposition) {
				this->glBindFramebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC) SDL_GL_GetProcAddress("glBindFramebufferEXT");
				if (glBindFramebuffer) {
					PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = (PFNGLGENFRAMEBUFFERSEXTPROC) SDL_GL_GetProcAddress("glGenFramebuffersEXT");
					assert(glGenFramebuffersEXT);
					glGenFramebuffers(1, &fbo);
					assert(glBindFramebuffer);
					glBindFramebuffer(GL_FRAMEBUFFER_EXT, fbo);

					PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC) SDL_GL_GetProcAddress("glFramebufferTexture2DEXT");
					assert(glFramebufferTexture2D);
					glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, fbt, 0);

					PFNGLDRAWBUFFERSPROC glDrawBuffers = (PFNGLDRAWBUFFERSARBPROC) SDL_GL_GetProcAddress("glDrawBuffersARB");
					if (glDrawBuffers) {
						GLenum drawBuffer0 = GL_COLOR_ATTACHMENT0;
						glDrawBuffers(1, &drawBuffer0);
					}

					PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC) SDL_GL_GetProcAddress("glCheckFramebufferStatusEXT");
					assert(glCheckFramebufferStatus);
					GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER_EXT);
					printf("Framebuffer status: %x (%d off)\n", (int) status, (int) status - GL_FRAMEBUFFER_COMPLETE_EXT);
					assert(status == GL_FRAMEBUFFER_COMPLETE_EXT);

					glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
				}

				this->glBlendColor = (PFNGLBLENDCOLOREXTPROC) SDL_GL_GetProcAddress("glBlendColorEXT");
			}

			this->glGenMipmap = (PFNGLGENERATEMIPMAPEXTPROC) SDL_GL_GetProcAddress("glGenerateMipmapEXT");

			this->previewImg = (intptr_t) fbt;
		}

		~StackedPreview() {
			if (!textures.empty()) {
				glDeleteTextures((int) textures.size(), textures.data());
			}
			if (fbo) {
				PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSEXTPROC) SDL_GL_GetProcAddress("glDeleteFramebuffersEXT");
				if (glDeleteFramebuffers)
					glDeleteFramebuffers(1, &fbo);
			}
			glDeleteTextures(1, &fbt);
		}

		void update(unsigned long long timeStamp, float const* const* data, double const volatile dataSamples[], int maxN) override {
			unsigned long long basetime = this->currentBasetime;
			if(basetime == 0)
				return; // generation not ready to be run

			assert(maxN <= (int) this->samples.size());
			int maxT = (maxN + (this->workersPerTarget - 1)) / this->workersPerTarget;
			assert(maxT <= (int) this->stamps.size());
			bool multiData = !this->textures.empty();

			float const* lastDataPtr = nullptr;
			for (int i = 0; i < maxT; ++i) {
				unsigned long long reftime = basetime > stamps[i] ? basetime + this->readyMS : stamps[i] + this->updateMS;
				if (timeStamp >= reftime) {
					bool targetUpdates = false;
					for (int j = i * workersPerTarget, je = std::min(j + workersPerTarget, maxN); j < je; ++j) {
						double dataSpp = dataSamples[j];
						float const* dataPtr = data[j];
						if (dataSpp) {
							(volatile double&) samples[j] = dataSpp;
							if (multiData) {
								glBindTexture(GL_TEXTURE_2D, textures[i]);
								glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, this->resX, this->resY, 0, GL_RGBA, GL_FLOAT, dataPtr);
								setupTexture(GL_TEXTURE_2D);
								glBindTexture(GL_TEXTURE_2D, 0);
							}
							lastDataPtr = dataPtr;
							targetUpdates = true;
						}
					}
					if (targetUpdates)
						(volatile unsigned long long&) stamps[i] = timeStamp;
				}
			}
			if (!lastDataPtr)
				return;
			float avgSamples = 0;
			for (int i = 0; i < maxN; ++i) {
				if (basetime <= stamps[i / workersPerTarget])
					avgSamples += float(samples[i]);
			}
			this->avgSamples = avgSamples;
			if (multiData && glBindFramebuffer) {
				glDisable(GL_CULL_FACE);
				glDisable(GL_DEPTH_TEST);
				glDisable(GL_LIGHTING);
				glDisable(GL_COLOR_MATERIAL);
				glEnableClientState(GL_VERTEX_ARRAY);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glEnable(GL_TEXTURE_2D);
				glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

				glMatrixMode(GL_PROJECTION);
				glLoadIdentity();
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();

				glBindFramebuffer(GL_FRAMEBUFFER_EXT, fbo);

				glDisable(GL_BLEND);

				glViewport(0, 0, resX, resY);
				glScissor(0, 0, resX, resY);
				glClearColor(0, 0, 0, 255);
				glClear(GL_COLOR_BUFFER_BIT);

				glEnable(GL_BLEND);
				glBlendFunc(GL_CONSTANT_COLOR, GL_ONE);

				float verts[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
				float texcs[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
				glVertexPointer(2, GL_FLOAT, sizeof(float) * 2, (const GLvoid*) verts);
				glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 2, (const GLvoid*) texcs);

				for (int i = 0; i < maxN; ++i) {
					if (samples[i] != 0) {
						float scaleFactor = 1 / std::max(avgSamples, 1.f);
						assert(glBlendColor);
						glBlendColor(scaleFactor, scaleFactor, scaleFactor, 0);
						glBindTexture(GL_TEXTURE_2D, textures[i]);
						glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
						glBindTexture(GL_TEXTURE_2D, 0);
					}
				}

				glDisable(GL_BLEND);
				glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);

				glDisableClientState(GL_VERTEX_ARRAY);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				glDisable(GL_TEXTURE_2D);
			}
			else {
				glBindTexture(GL_TEXTURE_2D, fbt);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, this->resX, this->resY, 0, GL_RGBA, GL_FLOAT, lastDataPtr);
				setupTexture(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
			}

			if (this->maxSubresLevels > 0 && glGenMipmap && avgSamples < 1) {
				float subresLevel = std::min(-std::log2(avgSamples), (float) this->maxSubresLevels) + this->subresBias;
				// off-center samples uniform at random:
				// -1/2 .. 0 .. 1/2, splatting 1/2 .. 1 .. 1/2 (in each dimension)
				// 2 integral(1 - x, 0, 1/2) = [2 x - x^2] = 1 - 1/4 = 3/4
				this->minSppClamp = (.75f * .75f) * std::pow(4.f, -subresLevel);
				glBindTexture(GL_TEXTURE_2D, fbt);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, this->maxSubresLevels);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, subresLevel);
				glGenMipmap(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, 0);
				haveMips = true;
			}
			else if (haveMips) {
				this->minSppClamp = 1;
				glBindTexture(GL_TEXTURE_2D, fbt);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 0);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
				glBindTexture(GL_TEXTURE_2D, 0);
				haveMips = false;
			}
		}
	};

} // namespace

StackedPreview* StackedPreview::create(int x, int y, int n, int m) {
	return new impl::StackedPreview(x, y, n, m);
}
StackedPreview::~StackedPreview() = default;
