#include <cstdlib>

#define Uses_TKeys
#define Uses_TApplication
#define Uses_TEvent
#define Uses_TRect
#define Uses_TDialog
#define Uses_TStaticText
#define Uses_TButton
#define Uses_TMenuBar
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TDeskTop
#define Uses_TEditWindow
#define Uses_TLabel
#define Uses_TInputLine
#define Uses_TCheckBoxes
#define Uses_TSItem
#define Uses_TMemo
#define Uses_TListBox
#define Uses_TScrollBar
#include <tvision/tv.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>

const int GreetThemCmd = 100;
const int cmSettings = 101;
const int cmHistory = 102;

class HistoryWindow : public TWindow
{
public:
    HistoryWindow(const TRect& bounds, TGroup *content, TScrollBar *scroll);
    void handleEvent(TEvent& event);
private:
    TGroup *content;
    TScrollBar *scroll;
};

HistoryWindow::HistoryWindow(const TRect& bounds, TGroup *aContent, TScrollBar *aScroll) :
    TWindowInit(&HistoryWindow::initFrame), TWindow(bounds, "History", wnNoNumber), content(aContent), scroll(aScroll)
{
    insert(content);
    insert(scroll);
}

void HistoryWindow::handleEvent(TEvent& event)
{
    if (event.what == evKeyDown)
    {
        switch (event.keyDown.keyCode)
        {
        case kbUp:
            scroll->setValue(scroll->value - 1);
            content->moveTo(0, -scroll->value);
            this->drawView();
            clearEvent(event);
            return;
        case kbDown:
            scroll->setValue(scroll->value + 1);
            content->moveTo(0, -scroll->value);
            this->drawView();
            clearEvent(event);
            return;
        case kbPgUp:
            scroll->setValue(scroll->value - (scroll->pgStep));
            content->moveTo(0, -scroll->value);
            this->drawView();
            clearEvent(event);
            return;
        case kbPgDn:
            scroll->setValue(scroll->value + (scroll->pgStep));
            content->moveTo(0, -scroll->value);
            this->drawView();
            clearEvent(event);
            return;
        }
    }
    if (event.what == evCommand && event.message.command == cmScrollBarChanged && event.message.infoPtr == scroll)
    {
        content->moveTo(0, -scroll->value);
        this->drawView();
        clearEvent(event);
        return;
    }
    TWindow::handleEvent(event);
}

class THelloApp : public TApplication
{

public:

    THelloApp();

    virtual void handleEvent( TEvent& event );
    static TMenuBar *initMenuBar( TRect );
    static TStatusLine *initStatusLine( TRect );

private:

    void greetingBox();
    void settingsDialog();
    void historyDialog();
};

THelloApp::THelloApp() :
    TProgInit( &THelloApp::initStatusLine,
               &THelloApp::initMenuBar,
               &THelloApp::initDeskTop
             )
{
}

void THelloApp::greetingBox()
{
    TRect r = deskTop->getExtent();
    deskTop->insert( new TEditWindow( r, 0, wnNoNumber ) );
}

void THelloApp::settingsDialog()
{
    TRect r = deskTop->getExtent();
    TDialog *d = new TDialog(r, "Settings");

    int centerX = (r.a.x + r.b.x) / 2;
    d->insert( new TButton( TRect(centerX - 15, 1, centerX - 3, 3), "~O~K", cmOK, bfDefault ) );
    d->insert( new TButton( TRect(centerX + 3, 1, centerX + 15, 3), "~C~ancel", cmCancel, bfNormal ) );

    d->insert( new TStaticText( TRect( 3, 4, 15, 5 ), "~N~ame" ) );
    d->insert( new TInputLine( TRect( 3, 5, 25, 6 ), 20 ) );
    d->insert( new TCheckBoxes( TRect( 3, 7, 20, 9 ), new TSItem( "~O~ption 1", new TSItem( "~O~ption 2", 0 ) ) ) );
    d->insert( new TStaticText( TRect( 3, 10, 15, 11 ), "~T~extbox" ) );
    d->insert( new TMemo( TRect( 3, 11, 40, 16 ), 0, 0, 0, 100 ) );
    d->insert( new TStaticText( TRect( 3, 17, 15, 18 ), "~S~elector" ) );
    d->insert( new TListBox( TRect( 3, 18, 20, 22 ), 1, 0 ) );

    deskTop->execView( d );
    destroy(d);
}

void THelloApp::historyDialog()
{
    TRect r = deskTop->getExtent();
    int totalHeight = 50 * 2 + 25 * 3;
    TGroup *content = new TGroup(TRect(0, 0, r.b.x - 2, totalHeight));
    int y = 0;
    for(int i = 0; i < 50; i++) {
        std::string s = "History " + std::to_string(i + 1);
        content->insert(new TStaticText(TRect(1, y, 30, y + 1), s.c_str()));
        y += 2;
    }
    for(int i = 0; i < 25; i++) {
        std::string s = "Btn " + std::to_string(i + 1);
        content->insert(new TButton(TRect(32, y, 45, y + 2), s.c_str(), cmOK, bfNormal));
        y += 3;
    }
    TScrollBar *vScroll = new TScrollBar(TRect(r.b.x - 1, 1, r.b.x, r.b.y - 1));
    vScroll->setRange(totalHeight, r.b.y - 2);
    HistoryWindow *d = new HistoryWindow(r, content, vScroll);
    deskTop->insert(d);
}

void THelloApp::handleEvent( TEvent& event )
{
    if( event.what == evKeyDown && event.keyDown.keyCode == kbAltX )
    {
        event.what = evCommand;
        event.message.command = cmQuit;
    }
    if( event.what == evCommand && event.message.command == cmQuit )
    {
        shutDown();
        exit(0);
    }
    TApplication::handleEvent( event );
    if( event.what == evCommand )
        {
        switch( event.message.command )
            {
            case GreetThemCmd:
                greetingBox();
                clearEvent( event );
                break;
            case cmSettings:
                settingsDialog();
                clearEvent( event );
                break;
            case cmHistory:
                historyDialog();
                clearEvent( event );
                break;
            default:
                break;
            }
        }
}

TMenuBar *THelloApp::initMenuBar( TRect r )
{

    r.b.y = r.a.y+1;

    return new TMenuBar( r,
      *new TSubMenu( "~H~ello", kbAltH ) +
        *new TMenuItem( "~E~dit Text...", GreetThemCmd, kbAltE ) +
        *new TMenuItem( "~P~ Settings...", cmSettings, kbAltP ) +
        *new TMenuItem( "~O~ History...", cmHistory, kbAltO ) +
         newLine() +
        *new TMenuItem( "E~x~it", cmQuit, cmQuit, hcNoContext, "Alt-X" )
        );

}

TStatusLine *THelloApp::initStatusLine( TRect r )
{
    r.a.y = r.b.y-1;
    return new TStatusLine( r,
        *new TStatusDef( 0, 0xFFFF ) +
            *new TStatusItem( "~Alt-X~ Exit", kbAltX, cmQuit ) +
            *new TStatusItem( "~Alt-E~ Edit", kbAltE, GreetThemCmd ) +
            *new TStatusItem( "~Alt-P~ Settings", kbAltP, cmSettings ) +
            *new TStatusItem( "~Alt-O~ History", kbAltO, cmHistory ) +
            *new TStatusItem( 0, kbF10, cmMenu )
            );
}

int main()
{
   setenv("COLORTERM", "truecolor", 1);
    THelloApp helloWorld;
    helloWorld.run();
    return 0;
}
