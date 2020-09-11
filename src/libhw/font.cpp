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

#include <mitsuba/render/font.h>
#include <mitsuba/hw/gputexture.h>

MTS_NAMESPACE_BEGIN

void Font::init(Renderer *renderer) {
	m_texture = renderer->createGPUTexture(m_name, m_bitmap);
	m_texture->setFilterType(GPUTexture::ENearest);
	m_texture->setMipMapped(false);
	m_texture->init();
}

void Font::cleanup() {
	m_texture->cleanup();
}

MTS_NAMESPACE_END
