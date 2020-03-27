/*
    This file is part of Mitsuba, a physically based rendering system.

    Copyright (c) 2007-2014 by Wenzel Jakob and others.

    Mitsuba is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License Version 3
    as published by the Free Software Foundation.

    Mitsuba is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <mitsuba/render/imageblock.h>

MTS_NAMESPACE_BEGIN

ImageBlock::ImageBlock(Bitmap::EPixelFormat fmt, const Vector2i &size,
		const ReconstructionFilter *filter, int channels, bool warn) : m_offset(0),
		m_size(size), m_filter(filter), m_weightsX(NULL), m_weightsY(NULL), m_warn(warn) {
	m_borderSize = filter ? filter->getBorderSize() : 0;

	/* Convert to multi-channel bitmap for cascaded rendering */
	if (filter && filter->cascade.count > 1) {
		int additionalChannels = (int) fmt - (int) Bitmap::ESpectrum;
		if (additionalChannels >= 0 && additionalChannels <= 2) {
			channels = SPECTRUM_SAMPLES;
			fmt = Bitmap::EMultiSpectrumAlphaWeight;
		}
		else if ((int) fmt < (int) Bitmap::ESpectrumAlphaWeight) {
			additionalChannels = (int) fmt & 0x1;
			channels = (int) fmt >= (int) Bitmap::ERGB ? 3 : 1;
			fmt = Bitmap::EMultiChannel;
		}
		else if (fmt == Bitmap::EMultiSpectrumAlphaWeight) {
			additionalChannels = 2;
			channels -= additionalChannels;
		}
		else {
			Assert((int) fmt >= (int) Bitmap::EMultiChannel);
			additionalChannels = 0;
		}
		channels *= filter->cascade.count;
		channels += additionalChannels;
		m_sharedChannels = additionalChannels;
	}
	else
		m_sharedChannels = 0;

	/* Allocate a small bitmap data structure for the block */
	m_bitmap = new Bitmap(fmt, Bitmap::EFloat,
		size + Vector2i(2 * m_borderSize), channels);
	m_normalChannels = m_bitmap->getChannelCount() - m_sharedChannels;
	m_bitmapSize = m_bitmap->getSize();
	m_floatData = m_bitmap->getFloatData();

	if (filter) {
		if (filter->cascade.count > 1)
			m_normalChannels /= filter->cascade.count;
		
		/* Temporary buffers used in put() */
		int tempBufferSize = (int) std::ceil(2*filter->getRadius()) + 1;
		m_weightsX = new Float[2*tempBufferSize];
		m_weightsY = m_weightsX + tempBufferSize;
	}
}

ImageBlock::~ImageBlock() {
	if (m_weightsX)
		delete[] m_weightsX;
}

void ImageBlock::load(Stream *stream) {
	m_offset = Point2i(stream);
	m_size = Vector2i(stream);
	stream->readFloatArray(
		m_bitmap->getFloatData(),
		(size_t) m_bitmap->getSize().x *
		(size_t) m_bitmap->getSize().y * m_bitmap->getChannelCount());
}

void ImageBlock::save(Stream *stream) const {
	m_offset.serialize(stream);
	m_size.serialize(stream);
	stream->writeFloatArray(
		m_bitmap->getFloatData(),
		(size_t) m_bitmap->getSize().x *
		(size_t) m_bitmap->getSize().y * m_bitmap->getChannelCount());
}


std::string ImageBlock::toString() const {
	std::ostringstream oss;
	oss << "ImageBlock[" << endl
		<< "  offset = " << m_offset.toString() << "," << endl
		<< "  size = " << m_size.toString() << "," << endl
		<< "  borderSize = " << m_borderSize << endl
		<< "]";
	return oss.str();
}

MTS_IMPLEMENT_CLASS(ImageBlock, false, WorkResult)
MTS_NAMESPACE_END
