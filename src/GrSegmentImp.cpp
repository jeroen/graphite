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
#include "processUTF.h"
#include <string.h>
#include <stdlib.h>
#include <new>

#include "GrSegmentImp.h"
#include "graphiteng/GrFont.h"
#include "CharInfoImp.h"
#include "SlotImp.h"
#include "Main.h"
#include "XmlTraceLog.h"
#include "CmapCache.h"
#include "SegCacheEntry.h"
#include "graphiteng/GrSegment.h"

namespace org { namespace sil { namespace graphite { namespace v2 {

GrSegment::GrSegment(unsigned int numchars, const GrFace* face, uint32 script, int textDir) :
        m_numGlyphs(numchars),
        m_numCharinfo(numchars),
        m_defaultOriginal(0),
        m_face(face),
        m_charinfo(new CharInfo[numchars]),
        m_silf(face->chooseSilf(script)),
        m_dir(textDir),
        m_freeSlots(NULL),
        m_last(NULL),
        m_first(NULL),
        m_bbox(Rect(Position(0, 0), Position(0, 0)))
{
    unsigned int i, j;
    m_bufSize = numchars + 10;
    freeSlot(newSlot());
    for (i = 0, j = 1; j < numchars; i++, j <<= 1) {}
    m_bufSize = i;                  // log2(numchars)
}

SegmentScopeState GrSegment::setScope(Slot * firstSlot, Slot * lastSlot, size_t subLength)
{
    SegmentScopeState state;
    state.numGlyphsOutsideScope = m_numGlyphs - subLength;
    state.realFirstSlot = m_first;
    state.slotBeforeScope = firstSlot->prev();
    state.slotAfterScope = lastSlot->next();
    state.realLastSlot = m_last;
    firstSlot->prev(NULL);
    lastSlot->next(NULL);
    assert(m_defaultOriginal == 0);
    m_defaultOriginal = firstSlot->original();
    m_numGlyphs = subLength;
    m_first = firstSlot;
    m_last = lastSlot;
    return state;
}

void GrSegment::removeScope(SegmentScopeState & state)
{
    m_numGlyphs = state.numGlyphsOutsideScope + m_numGlyphs;
    if (state.slotBeforeScope)
    {
        state.slotBeforeScope->next(m_first);
        m_first->prev(state.slotBeforeScope);
        m_first = state.realFirstSlot;
    }
    if (state.slotAfterScope)
    {
        state.slotAfterScope->prev(m_last);
        m_last->next(state.slotAfterScope);
        m_last = state.realLastSlot;
    }
    m_defaultOriginal = 0;
}


GrSegment::~GrSegment()
{
    SlotRope::iterator i;
    AttributeRope::iterator j;
    
    for (i = m_slots.begin(); i != m_slots.end(); i++)
        free(*i);
    for (j = m_userAttrs.begin(); j != m_userAttrs.end(); j++)
        free(*j);
    delete[] m_charinfo;
}

void GrSegment::append(const GrSegment &other)
{
    Rect bbox = other.m_bbox + m_advance;

    m_slots.insert(m_slots.end(), other.m_slots.begin(), other.m_slots.end());
    CharInfo* pNewCharInfo = new CharInfo[m_numCharinfo+other.m_numCharinfo];		//since CharInfo has no constructor, this doesn't do much
    for (unsigned int i=0 ; i<m_numCharinfo ; ++i)
	pNewCharInfo[i] = m_charinfo[i];
    m_last->next(other.m_first);
    other.m_last->prev(m_last);
    m_userAttrs.insert(m_userAttrs.end(), other.m_userAttrs.begin(), other.m_userAttrs.end());
    
    delete[] m_charinfo;
    m_charinfo = pNewCharInfo;
    pNewCharInfo += m_numCharinfo ;
    for (unsigned int i=0 ; i<m_numCharinfo ; ++i)
    {
	pNewCharInfo[i] = other.m_charinfo[i];
	pNewCharInfo[i].update(m_numCharinfo);
    }
 
    m_numCharinfo += other.m_numCharinfo;
    m_numGlyphs += other.m_numGlyphs;
    m_advance = m_advance + other.m_advance;
    m_bbox = m_bbox.widen(bbox);
}

void GrSegment::appendSlot(int id, int cid, int gid, int iFeats)
{
    Slot *aSlot = newSlot();
    
    m_charinfo[id].init(cid, id);
    m_charinfo[id].feats(iFeats);
    const GlyphFace * theGlyph = m_face->getGlyphFaceCache()->glyphSafe(gid);
    if (theGlyph)
    {
        m_charinfo[id].breakWeight(theGlyph->getAttr(m_silf->aBreak()));
    }
    else
    {
        m_charinfo[id].breakWeight(0);
    }
    
    aSlot->child(NULL);
    aSlot->setGlyph(this, gid, theGlyph);
    aSlot->originate(id);
    aSlot->before(id);
    aSlot->after(id);
    if (m_last) m_last->next(aSlot);
    aSlot->prev(m_last);
    m_last = aSlot;
    if (!m_first) m_first = aSlot;
}

Slot *GrSegment::newSlot()
{
    if (!m_freeSlots)
    {
        int numUser = m_silf->numUser();
        Slot *newSlots = grzeroalloc<Slot>(m_bufSize);
        uint16 *newAttrs = grzeroalloc<uint16>(numUser * m_bufSize);
        newSlots[0].userAttrs(newAttrs);
        for (size_t i = 1; i < m_bufSize - 1; i++)
        {
            newSlots[i].next(newSlots + i + 1);
            newSlots[i].userAttrs(newAttrs + i * numUser);
        }
        newSlots[m_bufSize - 1].userAttrs(newAttrs + (m_bufSize - 1) * numUser);
        newSlots[m_bufSize - 1].next(NULL);
        m_slots.push_back(newSlots);
        m_userAttrs.push_back(newAttrs);
        m_freeSlots = (m_bufSize > 1)? newSlots + 1 : NULL;
        return newSlots;
    }
    Slot *res = m_freeSlots;
    m_freeSlots = m_freeSlots->next();
    res->next(NULL);
    return res;
}

void GrSegment::freeSlot(Slot *aSlot)
{
    if (m_last == aSlot) m_last = aSlot->prev();
    if (m_first == aSlot) m_first = aSlot->next();
    // reset the slot incase it is reused
    ::new (aSlot) Slot;
    memset(aSlot->userAttrs(), 0, m_silf->numUser() * sizeof(uint16));
    // update next pointer
    if (!m_freeSlots)
        aSlot->next(NULL);
    else
        aSlot->next(m_freeSlots);
    m_freeSlots = aSlot;
}

void GrSegment::splice(size_t offset, size_t length, Slot * startSlot,
                       Slot * endSlot, const SegCacheEntry * entry)
{
    const Slot * replacement = entry->first();
    Slot * slot = startSlot;
    extendLength(entry->glyphLength() - length);
    // insert extra slots if needed
    while (entry->glyphLength() > length)
    {
        Slot * extra = newSlot();
        extra->prev(endSlot);
        extra->next(endSlot->next());
        endSlot->next(extra);
        if (extra->next())
            extra->next()->prev(extra);
        if (m_last == endSlot)
            m_last = extra;
        endSlot = extra;
        ++length;
    }
    // remove any extra
    if (entry->glyphLength() < length)
    {
        Slot * afterSplice = endSlot->next();
        do
        {
            endSlot = endSlot->prev();
            freeSlot(endSlot->next());
            --length;
        } while (entry->glyphLength() < length);
        endSlot->next(afterSplice);
        if (afterSplice)
            afterSplice->prev(endSlot);
    }
    assert(entry->glyphLength() == length);
    // keep a record of consecutive slots wrt start of splice to minimize
    // iterative next/prev calls
    Slot * slotArray[eMaxCachedSeg];
    uint16 slotPosition = 0;
    for (uint16 i = 0; i < entry->glyphLength(); i++)
    {
        if (slotPosition <= i)
        {
            slotArray[i] = slot;
            slotPosition = i;
        }
        slot->set(*replacement, offset, m_silf->numUser());
        if (replacement->attachedTo())
        {
            uint16 parentPos = replacement->attachedTo() - entry->first();
            while (slotPosition < parentPos)
            {
                slotArray[slotPosition+1] = slotArray[slotPosition]->next();
                ++slotPosition;
            }
            slot->attachTo(slotArray[parentPos]);
        }
        if (replacement->nextSibling())
        {
            uint16 pos = replacement->nextSibling() - entry->first();
            while (slotPosition < pos)
            {
                slotArray[slotPosition+1] = slotArray[slotPosition]->next();
                ++slotPosition;
            }
            slot->sibling(slotArray[pos]);
        }
        if (replacement->firstChild())
        {
            uint16 pos = replacement->firstChild() - entry->first();
            while (slotPosition < pos)
            {
                slotArray[slotPosition+1] = slotArray[slotPosition]->next();
                ++slotPosition;
            }
            slot->child(slotArray[pos]);
        }
        slot = slot->next();
        replacement = replacement->next();
    }
    // TODO charinfo slot associations
}

        
void GrSegment::positionSlots(const GrFont *font, Slot *iStart, Slot *iEnd)
{
    Position currpos;
    Slot *s;
    float cMin = 0.;
    Rect bbox;

    if (!iStart) iStart = m_first;
    if (!iEnd) iEnd = m_last;
    
    if (m_dir & 1)
    {
        for (s = iEnd; s && s != iStart->prev(); s = s->prev())
        {
            if (s->isBase())
                currpos = s->finalise(this, font, &currpos, &bbox, &cMin, 0);
        }
    }
    else
    {
        for (s = iStart; s && s != iEnd->next(); s = s->next())
        {
            if (s->isBase())
                currpos = s->finalise(this, font, &currpos, &bbox, &cMin, 0);
        }
    }
    if (iStart == m_first && iEnd == m_last) m_advance = currpos;
}

#ifndef DISABLE_TRACING
void GrSegment::logSegment(encform enc, const void* pStart, size_t nChars) const
{
    if (XmlTraceLog::get().active())
    {
        if (pStart)
        {
            XmlTraceLog::get().openElement(ElementText);
            XmlTraceLog::get().addAttribute(AttrEncoding, enc);
            XmlTraceLog::get().addAttribute(AttrLength, nChars);
            switch (enc)
            {
            case kutf8:
                XmlTraceLog::get().writeText(
                    reinterpret_cast<const char *>(pStart));
                break;
            case kutf16:
                for (size_t j = 0; j < nChars; ++j)
                {
                    uint32 code = reinterpret_cast<const uint16 *>(pStart)[j];
                    if (code >= 0xD800 && code <= 0xDBFF) // high surrogate
                    {
                        j++;
                        // append low surrogate
                        code = (code << 16) + reinterpret_cast<const uint16 *>(pStart)[j];
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                    {
                        XmlTraceLog::get().warning("Unexpected low surrogate %x at %d", code, j);
                    }
                    XmlTraceLog::get().writeUnicode(code);
                }
                break;
            case kutf32:
                for (size_t j = 0; j < nChars; ++j)
                {
                    XmlTraceLog::get().writeUnicode(
                        reinterpret_cast<const uint32 *>(pStart)[j]);
                }
                break;
            }
            XmlTraceLog::get().closeElement(ElementText);
        }
        logSegment();
    }
}

void GrSegment::logSegment() const
{
    if (XmlTraceLog::get().active())
    {
        XmlTraceLog::get().openElement(ElementSegment);
        XmlTraceLog::get().addAttribute(AttrLength, slotCount());
        XmlTraceLog::get().addAttribute(AttrAdvanceX, advance().x);
        XmlTraceLog::get().addAttribute(AttrAdvanceY, advance().y);
        for (Slot *i = m_first; i; i = i->next())
        {
            XmlTraceLog::get().openElement(ElementSlot);
            XmlTraceLog::get().addAttribute(AttrGlyphId, i->gid());
            XmlTraceLog::get().addAttribute(AttrX, i->origin().x);
            XmlTraceLog::get().addAttribute(AttrY, i->origin().y);
            XmlTraceLog::get().addAttribute(AttrBefore, i->before());
            XmlTraceLog::get().addAttribute(AttrAfter, i->after());
            XmlTraceLog::get().closeElement(ElementSlot);
        }
        XmlTraceLog::get().closeElement(ElementSegment);
    }
}

#endif



class SlotBuilder
{
public:
      SlotBuilder(const GrFace *face2, const Features* pFeats/*must not be NULL*/, GrSegment* pDest2)
      :	  m_face(face2), 
	  m_pDest(pDest2), 
	  m_ctable(TtfUtil::FindCmapSubtable(face2->getTable(tagCmap, NULL), 3, 1)),
	  m_stable(TtfUtil::FindCmapSubtable(face2->getTable(tagCmap, NULL), 3, 10)),
	  m_fid(pDest2->addFeatures(*pFeats)),
	  m_nCharsProcessed(0) 
      {
      }

      bool processChar(uint32 cid/*unicode character*/)		//return value indicates if should stop processing
      {
          uint16 realgid = 0;
	  uint16 gid = cid > 0xFFFF ? (m_stable ? TtfUtil::Cmap310Lookup(m_stable, cid) : 0) : TtfUtil::Cmap31Lookup(m_ctable, cid);
          if (!gid)
              gid = m_face->findPseudo(cid);
         // int16 bw = m_face->glyphAttr(gid, m_pDest->silf()->aBreak());
          m_pDest->appendSlot(m_nCharsProcessed, cid, gid, m_fid);
	  ++m_nCharsProcessed;
	  return true;
      }

      size_t charsProcessed() const { return m_nCharsProcessed; }

private:
      const GrFace *m_face;
      GrSegment *m_pDest;
      const void *const   m_ctable;
      const void *const   m_stable;
      const unsigned int m_fid;
      size_t m_nCharsProcessed ;
};

class CachedSlotBuilder
{
public:
    CachedSlotBuilder(const GrFace *face2, const Features* pFeats/*must not be NULL*/, GrSegment* pDest2)
    :  m_face(face2),
    m_cmap(face2->getCmapCache()),
    m_pDest(pDest2),
    m_breakAttr(pDest2->silf()->aBreak()),
    m_fid(pDest2->addFeatures(*pFeats)),
    m_nCharsProcessed(0)
    {
    }

    bool processChar(uint32 cid/*unicode character*/)     //return value indicates if should stop processing
    {
        uint16 realgid = 0;
        uint16 gid = m_cmap->lookup(cid);
        if (!gid)
            gid = m_face->findPseudo(cid);
        //int16 bw = m_face->glyphAttr(gid, m_breakAttr);
        m_pDest->appendSlot(m_nCharsProcessed, cid, gid, m_fid);
        ++m_nCharsProcessed;
        return true;
    }

      size_t charsProcessed() const { return m_nCharsProcessed; }

private:
      const GrFace *m_face;
      const CmapCache *m_cmap;
      GrSegment *m_pDest;
      uint8 m_breakAttr;
      const unsigned int m_fid;
      size_t m_nCharsProcessed ;
};

void GrSegment::read_text(const GrFace *face, const Features* pFeats/*must not be NULL*/, encform enc, const void* pStart, size_t nChars)
{
    assert(pFeats);
    CharacterCountLimit limit(enc, pStart, nChars);
    IgnoreErrors ignoreErrors;
    if (face->getCmapCache())
    {
        CachedSlotBuilder slotBuilder(face, pFeats, this);
        processUTF(limit/*when to stop processing*/, &slotBuilder, &ignoreErrors);
    }
    else
    {
        SlotBuilder slotBuilder(face, pFeats, this);
        processUTF(limit/*when to stop processing*/, &slotBuilder, &ignoreErrors);
    }
}

void GrSegment::prepare_pos(const GrFont *font)
{
    // copy key changeable metrics into slot (if any);
}

void GrSegment::finalise(const GrFont *font)
{
    positionSlots(font);
}

}}}} // namespace
