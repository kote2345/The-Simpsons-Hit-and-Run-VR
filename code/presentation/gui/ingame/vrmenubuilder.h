#ifndef VRMENUBUILDER_H
#define VRMENUBUILDER_H

#if defined(RAD_ANDROID)

#include <Page.h>
#include <Text.h>
#include <Group.h>
#include <FePage.h>
#include <FeGroup.h>
#include <FeText.h>
#include <raddebug.hpp>
#include <cstdio>

namespace VrMenuBuilder
{
struct Row
{
    Scrooby::Group* group;
    Scrooby::Text* label;
    Scrooby::Text* value;
};

inline unsigned int& GraphicsTextStyleId()
{
    static unsigned int id=0;
    return id;
}

inline bool& HasGraphicsTextStyle()
{
    static bool initialized=false;
    return initialized;
}

inline void RememberGraphicsStyle(FeText* style)
{
    if(!style) return;
    GraphicsTextStyleId()=style->GetTextStyleResourceId();
    HasGraphicsTextStyle()=true;
}

inline FeText* FindStyleText(Scrooby::Page* page)
{
    if(!page) return NULL;
    const char* const groups[]={"Menu","Tutorial","Camera","Display","Sound",
        "DisplayMode","ColourDepth","Resolution","Gamma","Settings"};
    const char* const texts[]={"Display","Tutorial","Camera","Sound","Settings",
        "DisplayMode","ColourDepth","Resolution","Gamma"};
    for(unsigned g=0;g<sizeof(groups)/sizeof(groups[0]);++g)
    {
        Scrooby::Group* group=page->GetGroup(groups[g]);
        if(!group) continue;
        for(unsigned t=0;t<sizeof(texts)/sizeof(texts[0]);++t)
            if(FeText* style=dynamic_cast<FeText*>(group->GetText(texts[t]))) return style;

        // Several FE pages put every row in a subgroup below "Menu".
        // The old lookup only inspected Menu's direct texts, so Display's
        // style was NULL in release builds and AddMenuItem dereferenced it.
        for(unsigned child=0;child<sizeof(groups)/sizeof(groups[0]);++child)
        {
            Scrooby::Group* subgroup=group->GetGroup(groups[child]);
            if(!subgroup) continue;
            for(unsigned t=0;t<sizeof(texts)/sizeof(texts[0]);++t)
                if(FeText* style=dynamic_cast<FeText*>(subgroup->GetText(texts[t]))) return style;
        }
    }
    for(unsigned t=0;t<sizeof(texts)/sizeof(texts[0]);++t)
        if(FeText* style=dynamic_cast<FeText*>(page->GetText(texts[t]))) return style;
    return NULL;
}

inline Row AddRow(Scrooby::Page* page,FeText* style,const char* prefix,int index,
                  const char* labelText,const char* const* values,int valueCount,
                  int firstY=120,int spacing=50,bool useGraphicsStyle=false)
{
    Row result={NULL,NULL,NULL};
    FePage* concretePage=dynamic_cast<FePage*>(page);
    if(!concretePage || !style || valueCount<1) return result;
    char groupName[64],labelName[64],valueName[64];
    std::sprintf(groupName,"%s_Row_%d",prefix,index);
    std::sprintf(labelName,"%s_Label_%d",prefix,index);
    std::sprintf(valueName,"%s_Value_%d",prefix,index);
    FeGroup* group=concretePage->AddGroup(groupName);
    group->Resize(2);
    const int rowY=firstY+index*spacing;
    FeText* label=group->AddText(labelName,105,rowY);
    label->Resize(1);
    FeText* value=group->AddText(valueName,365,rowY);
    value->Resize(valueCount);
    int styleW=300,styleH=42;
    style->GetBoundingBoxSize(styleW,styleH);
    label->SetBoundingBoxSize(250,styleH);
    value->SetBoundingBoxSize(190,styleH);
    const unsigned int textStyle=(useGraphicsStyle&&HasGraphicsTextStyle())?
        GraphicsTextStyleId():style->GetTextStyleResourceId();
    label->SetTextStyle(textStyle);
    value->SetTextStyle(textStyle);
    label->SetHorizontalJustification(Scrooby::Left);
    value->SetHorizontalJustification(Scrooby::Center);
    label->SetVerticalJustification(style->GetVerticalJustification());
    value->SetVerticalJustification(style->GetVerticalJustification());
    label->SetTextMode(style->GetTextMode());
    value->SetTextMode(style->GetTextMode());
    label->SetColour(tColour(255,255,255));
    value->SetColour(tColour(255,255,255));
    label->SetDisplayOutline(true); value->SetDisplayOutline(true);
    label->SetOutlineColour(tColour(0,0,0,192));
    value->SetOutlineColour(tColour(0,0,0,192));
    label->AddHardCodedString(labelText);
    for(int i=0;i<valueCount;++i) value->AddHardCodedString(values[i]);
    // FeGroup::Show() recomputes its origin from its children. Setting the
    // group's position here is therefore lost on the first Show(), which used
    // to collapse every generated row onto the same Y coordinate.
    group->Show();
    result.group=group; result.label=label; result.value=value;
    return result;
}
}

#endif
#endif
