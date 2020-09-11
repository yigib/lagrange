/* Copyright 2020 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "listwidget.h"
#include "scrollwidget.h"
#include "paint.h"
#include "util.h"
#include "command.h"

#include <the_Foundation/intset.h>

void init_ListItem(iListItem *d) {
    d->isSeparator = iFalse;
    d->isSelected  = iFalse;
}

void deinit_ListItem(iListItem *d) {
    iUnused(d);
}

iDefineObjectConstruction(ListItem)
iDefineClass(ListItem)

/*----------------------------------------------------------------------------------------------*/

iDefineObjectConstruction(ListWidget)

enum iBufferValidity {
    none_BufferValidity,
    partial_BufferValidity,
    full_BufferValidity,
};

#define numVisBuffers_ListWidget_   3

iDeclareType(ListVisBuffer)

struct Impl_ListVisBuffer {
    SDL_Texture *texture;
    int origin;
    iRangei validRange;
};

struct Impl_ListWidget {
    iWidget widget;
    iScrollWidget *scroll;
    int scrollY;
    int itemHeight;
    iPtrArray items;
    size_t hoverItem;
    iClick click;
    iIntSet invalidItems;
//    SDL_Texture *visBuffer[numVisBuffers_ListWidget_];
//    int visBufferIndex;
//    int visBufferScrollY[3];
//    enum iBufferValidity visBufferValid;
    iInt2 visBufSize;
    iListVisBuffer visBuffers[numVisBuffers_ListWidget_];
};

void init_ListWidget(iListWidget *d) {
    iWidget *w = as_Widget(d);
    init_Widget(w);
    setId_Widget(w, "list");
    setBackgroundColor_Widget(w, uiBackground_ColorId); /* needed for filling visbuffer */
    setFlags_Widget(w, hover_WidgetFlag, iTrue);
    addChild_Widget(w, iClob(d->scroll = new_ScrollWidget()));
    setThumb_ScrollWidget(d->scroll, 0, 0);
    d->scrollY = 0;
    init_PtrArray(&d->items);
    d->hoverItem = iInvalidPos;
    init_Click(&d->click, d, SDL_BUTTON_LEFT);
    init_IntSet(&d->invalidItems);
    d->visBufSize = zero_I2();
    iZap(d->visBuffers);
}

void deinit_ListWidget(iListWidget *d) {
    clear_ListWidget(d);
    deinit_PtrArray(&d->items);
    iForIndices(i, d->visBuffers) {
        SDL_DestroyTexture(d->visBuffers[i].texture);
    }
}

void invalidate_ListWidget(iListWidget *d) {
    //d->visBufferValid = none_BufferValidity;
    iForIndices(i, d->visBuffers) {
        iZap(d->visBuffers[i].validRange);
    }
    clear_IntSet(&d->invalidItems); /* all will be drawn */
    refresh_Widget(as_Widget(d));
}

void invalidateItem_ListWidget(iListWidget *d, size_t index) {
    insert_IntSet(&d->invalidItems, index);
    refresh_Widget(d);
}

void clear_ListWidget(iListWidget *d) {
    iForEach(PtrArray, i, &d->items) {
        deref_Object(i.ptr);
    }
    clear_PtrArray(&d->items);
    d->hoverItem = iInvalidPos;
}

void addItem_ListWidget(iListWidget *d, iAnyObject *item) {
    pushBack_PtrArray(&d->items, ref_Object(item));
}

iScrollWidget *scroll_ListWidget(iListWidget *d) {
    return d->scroll;
}

size_t numItems_ListWidget(const iListWidget *d) {
    return size_PtrArray(&d->items);
}

static int scrollMax_ListWidget_(const iListWidget *d) {
    return iMax(0,
                (int) size_PtrArray(&d->items) * d->itemHeight -
                    height_Rect(innerBounds_Widget(constAs_Widget(d))));
}

void updateVisible_ListWidget(iListWidget *d) {
    const int   contentSize = size_PtrArray(&d->items) * d->itemHeight;
    const iRect bounds      = innerBounds_Widget(as_Widget(d));
    const iBool wasVisible  = isVisible_Widget(d->scroll);
    setRange_ScrollWidget(d->scroll, (iRangei){ 0, scrollMax_ListWidget_(d) });
    setThumb_ScrollWidget(d->scroll,
                          d->scrollY,
                          contentSize > 0 ? height_Rect(bounds_Widget(as_Widget(d->scroll))) *
                                                height_Rect(bounds) / contentSize
                                          : 0);
    if (wasVisible != isVisible_Widget(d->scroll)) {
        invalidate_ListWidget(d); /* clip margins changed */
    }
}

void setItemHeight_ListWidget(iListWidget *d, int itemHeight) {
    d->itemHeight = itemHeight;
    invalidate_ListWidget(d);
}

int itemHeight_ListWidget(const iListWidget *d) {
    return d->itemHeight;
}

int scrollPos_ListWidget(const iListWidget *d) {
    return d->scrollY;
}

void setScrollPos_ListWidget(iListWidget *d, int pos) {
    d->scrollY = pos;
    d->hoverItem = iInvalidPos;
//    d->visBufferValid = partial_BufferValidity;
    refresh_Widget(as_Widget(d));
}

void scrollOffset_ListWidget(iListWidget *d, int offset) {
    const int oldScroll = d->scrollY;
    d->scrollY += offset;
    if (d->scrollY < 0) {
        d->scrollY = 0;
    }
    const int scrollMax = scrollMax_ListWidget_(d);
    d->scrollY = iMin(d->scrollY, scrollMax);
    if (oldScroll != d->scrollY) {
        if (d->hoverItem != iInvalidPos) {
            invalidateItem_ListWidget(d, d->hoverItem);
            d->hoverItem = iInvalidPos;
        }
        updateVisible_ListWidget(d);
//        d->visBufferValid = partial_BufferValidity;
        refresh_Widget(as_Widget(d));
    }
}

void scrollToItem_ListWidget(iListWidget *d, size_t index) {
    const iRect rect    = innerBounds_Widget(as_Widget(d));
    int         yTop    = d->itemHeight * index - d->scrollY;
    int         yBottom = yTop + d->itemHeight;
    if (yBottom > height_Rect(rect)) {
        scrollOffset_ListWidget(d, yBottom - height_Rect(rect));
    }
    else if (yTop < 0) {
        scrollOffset_ListWidget(d, yTop);
    }
}

int visCount_ListWidget(const iListWidget *d) {
    return iMin(height_Rect(innerBounds_Widget(constAs_Widget(d))) / d->itemHeight,
                (int) size_PtrArray(&d->items));
}

static iRanges visRange_ListWidget_(const iListWidget *d) {
    if (d->itemHeight == 0) {
        return (iRanges){ 0, 0 };
    }
    iRanges vis = { d->scrollY / d->itemHeight, 0 };
    vis.end = iMin(size_PtrArray(&d->items), vis.start + visCount_ListWidget(d) + 1);
    return vis;
}

size_t itemIndex_ListWidget(const iListWidget *d, iInt2 pos) {
    const iRect bounds = innerBounds_Widget(constAs_Widget(d));
    pos.y -= top_Rect(bounds) - d->scrollY;
    if (pos.y < 0 || !d->itemHeight) return iInvalidPos;
    size_t index = pos.y / d->itemHeight;
    if (index >= size_Array(&d->items)) return iInvalidPos;
    return index;
}

const iAnyObject *constItem_ListWidget(const iListWidget *d, size_t index) {
    if (index < size_PtrArray(&d->items)) {
        return constAt_PtrArray(&d->items, index);
    }
    return NULL;
}

const iAnyObject *constHoverItem_ListWidget(const iListWidget *d) {
    return constItem_ListWidget(d, d->hoverItem);
}

iAnyObject *item_ListWidget(iListWidget *d, size_t index) {
    if (index < size_PtrArray(&d->items)) {
        return at_PtrArray(&d->items, index);
    }
    return NULL;
}

iAnyObject *hoverItem_ListWidget(iListWidget *d) {
    return item_ListWidget(d, d->hoverItem);
}

static void setHoverItem_ListWidget_(iListWidget *d, size_t index) {
    if (index < size_PtrArray(&d->items)) {
        const iListItem *item = at_PtrArray(&d->items, index);
        if (item->isSeparator) {
            index = iInvalidPos;
        }
    }
    if (d->hoverItem != index) {
        insert_IntSet(&d->invalidItems, d->hoverItem);
        insert_IntSet(&d->invalidItems, index);
        d->hoverItem = index;
        refresh_Widget(as_Widget(d));
    }
}

void updateMouseHover_ListWidget(iListWidget *d) {
    const iInt2 mouse = mouseCoord_Window(get_Window());
    setHoverItem_ListWidget_(d, itemIndex_ListWidget(d, mouse));
}

static void redrawHoverItem_ListWidget_(iListWidget *d) {
    insert_IntSet(&d->invalidItems, d->hoverItem);
    refresh_Widget(as_Widget(d));
}

static iBool processEvent_ListWidget_(iListWidget *d, const SDL_Event *ev) {
    iWidget *w = as_Widget(d);
    if (isCommand_SDLEvent(ev)) {
        const char *cmd = command_UserEvent(ev);
        if (equal_Command(cmd, "theme.changed")) {
            invalidate_ListWidget(d);
        }
        else if (isCommand_Widget(w, ev, "scroll.moved")) {
            setScrollPos_ListWidget(d, arg_Command(cmd));
            return iTrue;
        }
    }
    if (ev->type == SDL_MOUSEMOTION) {
        const iInt2 mouse = init_I2(ev->motion.x, ev->motion.y);
        size_t hover = iInvalidPos;
        if (!contains_Widget(constAs_Widget(d->scroll), mouse) &&
            contains_Widget(w, mouse)) {
            hover = itemIndex_ListWidget(d, mouse);
        }
        setHoverItem_ListWidget_(d, hover);
    }
    if (ev->type == SDL_MOUSEWHEEL && isHover_Widget(w)) {
#if defined (iPlatformApple)
        /* Momentum scrolling. */
        scrollOffset_ListWidget(d, -ev->wheel.y * get_Window()->pixelRatio);
#else
        scrollOffset_ListWidget(d, -ev->wheel.y * 3 * d->itemHeight);
#endif
        return iTrue;
    }
    switch (processEvent_Click(&d->click, ev)) {
        case started_ClickResult:
            redrawHoverItem_ListWidget_(d);
            return iTrue;
        case aborted_ClickResult:
            redrawHoverItem_ListWidget_(d);
            break;
        case finished_ClickResult:
        case double_ClickResult:
            redrawHoverItem_ListWidget_(d);
            if (contains_Rect(innerBounds_Widget(w), pos_Click(&d->click)) &&
                d->hoverItem != iInvalidSize) {
                postCommand_Widget(w, "list.clicked arg:%zu item:%p",
                                   d->hoverItem, constHoverItem_ListWidget(d));
            }
            return iTrue;
        default:
            break;
    }
    return processEvent_Widget(w, ev);
}

static void allocVisBuffer_ListWidget_(iListWidget *d) {
    /* Make sure two buffers cover the entire visible area. */
    const iInt2 size = div_I2(addY_I2(innerBounds_Widget(as_Widget(d)).size, 1), init_I2(1, 2));
    if (!d->visBuffers[0].texture || !isEqual_I2(size, d->visBufSize)) {
        d->visBufSize = size;
        iForIndices(i, d->visBuffers) {
            if (d->visBuffers[i].texture) {
                SDL_DestroyTexture(d->visBuffers[i].texture);
            }
            d->visBuffers[i].texture =
                SDL_CreateTexture(renderer_Window(get_Window()),
                                  SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_STATIC | SDL_TEXTUREACCESS_TARGET,
                                  size.x,
                                  size.y);
            SDL_SetTextureBlendMode(d->visBuffers[i].texture, SDL_BLENDMODE_NONE);
            d->visBuffers[i].origin = i * size.y;
            iZap(d->visBuffers[i].validRange);
        }
        //d->visBufferValid = none_BufferValidity;
    }
}

static void drawItem_ListWidget_(const iListWidget *d, iPaint *p, size_t index, iInt2 pos) {
    const iWidget *  w         = constAs_Widget(d);
    const iRect      bounds    = innerBounds_Widget(w);
    const iRect      bufBounds = { zero_I2(), bounds.size };
    const iListItem *item      = constAt_PtrArray(&d->items, index);
    const iRect      itemRect  = { pos, init_I2(width_Rect(bounds), d->itemHeight) };
//    setClip_Paint(p, intersect_Rect(itemRect, bufBounds));
//    if (d->visBufferValid) {
//        fillRect_Paint(p, itemRect, w->bgColor);
//    }
    class_ListItem(item)->draw(item, p, itemRect, d);
//    unsetClip_Paint(p);
}

static const iListItem *item_ListWidget_(const iListWidget *d, size_t pos) {
    return constAt_PtrArray(&d->items, pos);
}

#if 0
static size_t findBuffer_ListWidget_(const iListWidget *d, int top, const iRangei vis) {
    size_t avail = iInvalidPos;
    iForIndices(i, d->visBuffers) {
        const iListVisBuffer *buf = d->visBuffers + i;
        const iRangei bufRange = { buf->scrollY, buf->scrollY + d->visBufSize.y };
        if (top >= bufRange.start && top < bufRange.end) {
            return i;
        }
        if (buf->scrollY >= vis.end || buf->scrollY + d->visBufSize.y <= vis.start) {
            avail = i; /* Outside currently visible region. */
        }
    }
    iAssert(avail != iInvalidPos);
    return avail;
}
#endif

static void draw_ListWidget_(const iListWidget *d) {
    const iWidget *w         = constAs_Widget(d);
    const iRect    bounds    = innerBounds_Widget(w);
    if (!bounds.size.y || !bounds.size.x) return;
    iPaint p;
    init_Paint(&p);
    SDL_Renderer *render = renderer_Window(get_Window());
    drawBackground_Widget(w);
    iListWidget *m = iConstCast(iListWidget *, d);
    allocVisBuffer_ListWidget_(m);
    /* Update invalid regions/items. */
    if (d->visBufSize.y > 0) {
    /*if (d->visBufferValid != full_BufferValidity || !isEmpty_IntSet(&d->invalidItems)) */
        iAssert(d->visBuffers[0].texture);
        iAssert(d->visBuffers[1].texture);
        iAssert(d->visBuffers[2].texture);
//        const int vbSrc = d->visBufferIndex;
//        const int vbDst = d->visBufferIndex ^ 1;
        const int bottom = numItems_ListWidget(d) * d->itemHeight;
        const iRangei vis = { d->scrollY, d->scrollY + bounds.size.y };
        iRangei good = { 0, 0 };
        printf("visBufSize.y = %d\n", d->visBufSize.y);
        size_t avail[3], numAvail = 0;
        /* Check which buffers are available for reuse. */ {
            iForIndices(i, d->visBuffers) {
                iListVisBuffer *buf = m->visBuffers + i;
                const iRangei region = { buf->origin, buf->origin + d->visBufSize.y };
                if (region.start >= vis.end || region.end <= vis.start) {
                    avail[numAvail++] = i;
                    iZap(buf->validRange);
                }
                else {
                    good = union_Rangei(good, region);
                }
            }
        }
        if (numAvail == numVisBuffers_ListWidget_) {
            /* All buffers are outside the visible range, do a reset. */
            m->visBuffers[0].origin = vis.start;
            m->visBuffers[1].origin = vis.start + d->visBufSize.y;
        }
        else {
            /* Extend to cover the visible range. */
            while (vis.start < good.start) {
                iAssert(numAvail > 0);
                m->visBuffers[avail[--numAvail]].origin = good.start - d->visBufSize.y;
                good.start -= d->visBufSize.y;
            }
            while (vis.end > good.end) {
                iAssert(numAvail > 0);
                m->visBuffers[avail[--numAvail]].origin = good.end;
                good.end += d->visBufSize.y;
            }
        }
        /* Check which parts are invalid. */
        iRangei invalidRange[3];
        iForIndices(i, d->visBuffers) {
            const iListVisBuffer *buf = d->visBuffers + i;
            const iRangei region = intersect_Rangei(vis, (iRangei){ buf->origin, buf->origin + d->visBufSize.y });
            const iRangei before = { 0, buf->validRange.start };
            const iRangei after  = { buf->validRange.end, bottom };
            invalidRange[i] = intersect_Rangei(before, region);
            if (isEmpty_Rangei(invalidRange[i])) {
                invalidRange[i] = intersect_Rangei(after, region);
            }
        }
        iForIndices(i, d->visBuffers) {
            iListVisBuffer *buf = m->visBuffers + i;
            printf("%zu: orig %d, invalid %d ... %d\n", i, buf->origin, invalidRange[i].start, invalidRange[i].end);
            iRanges drawItems = { iMax(0, buf->origin) / d->itemHeight,
                                  iMax(0, buf->origin + d->visBufSize.y) / d->itemHeight + 1 };
            iBool isTargetSet = iFalse;
            if (isEmpty_Rangei(buf->validRange)) {
                isTargetSet = iTrue;
                beginTarget_Paint(&p, buf->texture);
                fillRect_Paint(&p, (iRect){ zero_I2(), d->visBufSize }, w->bgColor);
            }
            iConstForEach(IntSet, v, &d->invalidItems) {
                const size_t index = *v.value;
                if (contains_Range(&drawItems, index)) {
                    const iListItem *item = constAt_PtrArray(&d->items, index);
                    const iRect      itemRect = { init_I2(0, index * d->itemHeight - buf->origin),
                                                  init_I2(d->visBufSize.x, d->itemHeight) };
                    if (!isTargetSet) {
                        beginTarget_Paint(&p, buf->texture);
                        isTargetSet = iTrue;
                    }
                    fillRect_Paint(&p, itemRect, w->bgColor);
                    class_ListItem(item)->draw(item, &p, itemRect, d);
                    printf("- drawing invalid item %zu\n", index);
                }
            }
            if (!isEmpty_Rangei(invalidRange[i])) {
                if (!isTargetSet) {
                    beginTarget_Paint(&p, buf->texture);
                    isTargetSet = iTrue;
                }
                /* Visible range is not fully covered. Fill in the new items. */
//                fillRect_Paint(&p, (iRect){ init_I2(0, invalidRange[i].start - buf->origin),
//                                            init_I2(d->visBufSize.x, invalidRange[i].end - buf->origin) },
//                               w->bgColor);
                drawItems.start = invalidRange[i].start / d->itemHeight;
                drawItems.end   = invalidRange[i].end   / d->itemHeight + 1;
                for (size_t j = drawItems.start; j < drawItems.end && j < size_PtrArray(&d->items); j++) {
                    const iListItem *item     = constAt_PtrArray(&d->items, j);
                    const iRect      itemRect = { init_I2(0, j * d->itemHeight - buf->origin),
                                                  init_I2(d->visBufSize.x, d->itemHeight) };
                    fillRect_Paint(&p, itemRect, w->bgColor);
                    class_ListItem(item)->draw(item, &p, itemRect, d);
                    printf("- drawing item %zu\n", j);
                }
            }
            /* TODO: Redraw invalidated items. */
            if (isTargetSet) {
                endTarget_Paint(&p);
            }
            buf->validRange =
                intersect_Rangei(vis, (iRangei){ buf->origin, buf->origin + d->visBufSize.y });
            fflush(stdout);
        }
#if 0
        beginTarget_Paint(&p, d->visBuffer[vbDst]);
        const iRect bufBounds = (iRect){ zero_I2(), bounds.size };
        iRanges invalidRange = { 0, 0 };
        if (!d->visBufferValid) {
            fillRect_Paint(&p, bufBounds, w->bgColor);
        }
        else if (d->visBufferValid == partial_BufferValidity) {
            /* Copy previous contents. */
            const int delta = d->scrollY - d->visBufferScrollY;
            SDL_RenderCopy(
                render,
                d->visBuffer[vbSrc],
                NULL,
                &(SDL_Rect){ 0, -delta, bounds.size.x, bounds.size.y });
            if (delta > 0) {
                /* Scrolling down. */
                invalidRange.start = (d->visBufferScrollY + bounds.size.y) / d->itemHeight;
                invalidRange.end   = (d->scrollY + bounds.size.y) / d->itemHeight + 1;
            }
            else if (delta < 0) {
                /* Scrolling up. */
                invalidRange.start = d->scrollY / d->itemHeight;
                invalidRange.end   = d->visBufferScrollY / d->itemHeight + 1;
            }
        }
        /* Draw items. */ {
            const iRanges visRange = visRange_ListWidget_(d);
            iInt2         pos      = init_I2(0, -(d->scrollY % d->itemHeight));
            for (size_t i = visRange.start; i < visRange.end; i++) {
                /* TODO: Refactor to loop through invalidItems only. */
                if (!d->visBufferValid || contains_Range(&invalidRange, i) ||
                    contains_IntSet(&d->invalidItems, i)) {
                    const iListItem *item     = constAt_PtrArray(&d->items, i);
                    const iRect      itemRect = { pos, init_I2(width_Rect(bounds), d->itemHeight) };
                    setClip_Paint(&p, intersect_Rect(itemRect, bufBounds));
                    if (d->visBufferValid) {
                        fillRect_Paint(&p, itemRect, w->bgColor);
                    }
                    class_ListItem(item)->draw(item, &p, itemRect, d);
                    /* Clear under the scrollbar. */
                    if (isVisible_Widget(d->scroll)) {
                        fillRect_Paint(
                            &p,
                            (iRect){ addX_I2(topRight_Rect(itemRect), -width_Widget(d->scroll)),
                                     bottomRight_Rect(itemRect) },
                            w->bgColor);
                    }
                    unsetClip_Paint(&p);                    
                }
                pos.y += d->itemHeight;
            }
        }
        endTarget_Paint(&p);
        /* Update state. */
//        m->visBufferValid = iTrue;
//        m->visBufferScrollY = m->scrollY;
//        m->visBufferIndex = vbDst;
#endif
        clear_IntSet(&m->invalidItems);
    }
    //SDL_RenderCopy(render, d->visBuffer[d->visBufferIndex], NULL, (const SDL_Rect *) &bounds);
    setClip_Paint(&p, bounds_Widget(w));
    iForIndices(i, d->visBuffers) {
        const iListVisBuffer *buf = d->visBuffers + i;
        SDL_RenderCopy(render,
                       buf->texture,
                       NULL,
                       &(SDL_Rect){ left_Rect(bounds),
                                    top_Rect(bounds) - d->scrollY + buf->origin,
                                    d->visBufSize.x,
                                    d->visBufSize.y });
    }
    unsetClip_Paint(&p);
    drawChildren_Widget(w);
}

iBool isMouseDown_ListWidget(const iListWidget *d) {
    return d->click.isActive &&
           contains_Rect(innerBounds_Widget(constAs_Widget(d)), pos_Click(&d->click));
}

iBeginDefineSubclass(ListWidget, Widget)
    .processEvent = (iAny *) processEvent_ListWidget_,
    .draw         = (iAny *) draw_ListWidget_,
iEndDefineSubclass(ListWidget)
