/*  GRAPHITENG LICENSING

    Copyright 2010, SIL International
    All rights reserved.

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should also have received a copy of the GNU Lesser General Public
    License along with this library in the file named "LICENSE".
    If not, write to the Free Software Foundation, Inc., 59 Temple Place,
    Suite 330, Boston, MA 02111-1307, USA or visit their web page on the
    internet at http://www.fsf.org/licenses/lgpl.html.
*/

#include "Main.h"
#include "CmapCache.h"
#include "TtfTypes.h"
#include "TtfUtil.h"

namespace org { namespace sil { namespace graphite { namespace v2 {

CmapCache::CmapCache(const void* cmapTable)
{
    void * table31 = TtfUtil::FindCmapSubtable(cmapTable,3, 1);
    void * table310 = TtfUtil::FindCmapSubtable(cmapTable,3, 10);
    m_isBmpOnly = (!table310);
    int rangeKey = 0;
    unsigned int codePoint = 0;
    if (table310 && TtfUtil::CheckCmap310Subtable(table310))
    {
        m_blocks = grzeroalloc<uint16*>(0x1100);
        if (!m_blocks) return;
        codePoint =  TtfUtil::Cmap310NextCodepoint(table310, codePoint, &rangeKey);
        while (codePoint != 0x10FFFF)
        {
            unsigned int block = (codePoint & 0xFFFF00) >> 8;
            if (!m_blocks[block])
            {
                m_blocks[block] = grzeroalloc<uint16>(0x100);
                if (!m_blocks[block])
                    return;
            }
            m_blocks[block][codePoint & 0xFF] = TtfUtil::Cmap310Lookup(table310, codePoint);
            codePoint =  TtfUtil::Cmap310NextCodepoint(table310, codePoint, &rangeKey);
        }
    }
    else
    {
        m_blocks = grzeroalloc<uint16*>(0x100);
        if (!m_blocks) return;
    }
    if (table31 && TtfUtil::CheckCmap31Subtable(table31))
    {
        codePoint = 0;
        rangeKey = 0;
        codePoint =  TtfUtil::Cmap31NextCodepoint(table31, codePoint, &rangeKey);
        while (codePoint != 0xFFFF)
        {
            unsigned int block = (codePoint & 0xFFFF00) >> 8;
            if (!m_blocks[block])
            {
                m_blocks[block] = grzeroalloc<uint16>(0x100);
                if (!m_blocks[block])
                    return;
            }
            m_blocks[block][codePoint & 0xFF] = TtfUtil::Cmap31Lookup(table31, codePoint);
            codePoint =  TtfUtil::Cmap31NextCodepoint(table31, codePoint, &rangeKey);
        }
    }
}

CmapCache::~CmapCache()
{
    unsigned int numBlocks = (m_isBmpOnly)? 0x100 : 0x1100;
    for (unsigned int i = 0; i < numBlocks; i++)
    {
        if (m_blocks[i])
            free(m_blocks[i]);
    }
    free(m_blocks);
    m_blocks = NULL;
}


}}}} // namespace
