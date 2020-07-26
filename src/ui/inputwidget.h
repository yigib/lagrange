#pragma once

#include "widget.h"

iDeclareWidgetClass(InputWidget)
iDeclareObjectConstructionArgs(InputWidget, size_t maxLen)

enum iInputMode {
    insert_InputMode,
    overwrite_InputMode,
};

void    setSensitive_InputWidget(iInputWidget *, iBool isSensitive);
void    setMode_InputWidget     (iInputWidget *, enum iInputMode mode);
void    setMaxLen_InputWidget   (iInputWidget *, size_t maxLen);
void    setText_InputWidget     (iInputWidget *, const iString *text);
void    setTextCStr_InputWidget (iInputWidget *, const char *cstr);
void    setCursor_InputWidget   (iInputWidget *, size_t pos);
void    begin_InputWidget       (iInputWidget *);
void    end_InputWidget         (iInputWidget *, iBool accept);

const iString * text_InputWidget    (const iInputWidget *);
