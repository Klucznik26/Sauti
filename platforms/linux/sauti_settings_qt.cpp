#include <QApplication>
#include <QButtonGroup>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <functional>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QTimer>
#include <QEnterEvent>
#include <cmath>
#include <vector>
#include <cstring>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <memory>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QStandardPaths>
#include <QFrame>
#include <QRadioButton>
#include <QGridLayout>
#include <QComboBox>
#include <QScrollArea>
#include <QTextStream>
#include <QFont>

static int currentLanguage=1;
static QString L(const char *english){
    static const struct {const char *en,*pl,*fr;} translations[]={
    {"SAYARI  /  SAUTI","SAYARI  /  SAUTI","SAYARI  /  SAUTI"},
    {"PREFERENCES  ·  AUDIO WORKSTATION","USTAWIENIA  ·  ODTWARZACZ AUDIO","PRÉFÉRENCES  ·  STATION AUDIO"},
    {"AUDIO","DŹWIĘK","AUDIO"},
    {"APPEARANCE","WYGLĄD","APPARENCE"},
    {"LIBRARY","BIBLIOTEKA","MÉDIATHÈQUE"},
    {"LANGUAGE","JĘZYK","LANGUE"},
    {"CONTROLS","STEROWANIE","COMMANDES"},
    {"INFO","INFO","INFOS"},
    {"Audio output","Wyjście dźwięku","Sortie audio"},
    {"The active playback path and sample format","Aktywny tor odtwarzania i format próbek","Chaîne de lecture active et format des échantillons"},
    {"Playback backend","Silnik odtwarzania","Moteur de lecture"},
    {"SDL2 / Float32 — active","SDL2 / Float32 — aktywny","SDL2 / Float32 — actif"},
    {"Native PipeWire playback and backend switching are in development.","Natywne odtwarzanie PipeWire i przełączanie silników są w przygotowaniu.","La lecture native PipeWire et le changement de moteur sont en développement."},
    {"Signal path","Tor sygnału","Chaîne du signal"},
    {"Decoder → Float32 PCM → SDL2 → system audio","Dekoder → PCM Float32 → SDL2 → dźwięk systemowy","Décodeur → PCM Float32 → SDL2 → sortie système"},
    {"Source sample rate is requested; final hardware format depends on PipeWire.","Wymagana jest częstotliwość źródła; format sprzętowy zależy od PipeWire.","La fréquence source est demandée ; le format matériel dépend de PipeWire."},
    {"Appearance","Wygląd","Apparence"},
    {"Personalize the spectrum workspace without affecting audio","Dostosuj wygląd analizatora bez wpływu na dźwięk","Personnalisez l'analyseur sans modifier le son"},
    {"Accent","Akcent kolorystyczny","Couleur d'accent"},
    {"Emerald","Szmaragdowy","Émeraude"},
    {"Violet","Fioletowy","Violet"},
    {"Blue","Niebieski","Bleu"},
    {"Workspace background","Tło odtwarzacza","Arrière-plan du lecteur"},
    {"Frame color","Kolor ramek","Couleur des cadres"},
    {"Gray","Szary","Gris"},
    {"Sage","Szałwiowy","Sauge"},
    {"Mist blue","Błękit mglisty","Bleu brume"},
    {"Amber","Bursztynowy","Ambre"},
    {"Forest","Leśny","Forêt"},
    {"Original","Oryginalny","Original"},
    {"Burgundy","Bordowy","Bordeaux"},
    {"Navy","Granatowy","Bleu marine"},
    {"Music library","Biblioteka muzyczna","Bibliothèque musicale"},
    {"File locations and browsing","Lokalizacje plików i przeglądanie","Emplacements des fichiers et navigation"},
    {"Default music folder","Domyślny folder muzyki","Dossier musical par défaut"},
    {"Current directory","Aktualny folder","Dossier actuel"},
    {"Choose folder…","Wybierz folder…","Choisir un dossier…"},
    {"Save as default","Zapisz jako domyślny","Enregistrer par défaut"},
    {"The chosen location becomes the starting folder in Sauti.","Wybrany folder będzie folderem startowym w Sauti.","Le dossier choisi deviendra le dossier de démarrage de Sauti."},
    {"Choose music folder","Wybierz folder muzyki","Choisir le dossier musical"},
    {"Folder saved successfully.","Zapisano folder domyślny.","Dossier par défaut enregistré."},
    {"Could not save the folder.","Nie udało się zapisać folderu.","Impossible d'enregistrer le dossier."},
    {"Use the FOLDER button in the main Sauti window to choose and save the starting folder.","Użyj przycisku FOLDER w głównym oknie Sauti, aby wybrać i zapisać folder początkowy.","Utilisez FOLDER dans la fenêtre principale pour choisir et enregistrer le dossier initial."},
    {"Language","Język","Langue"},
    {"Select the language used by Sauti","Wybierz język interfejsu Sauti","Choisissez la langue de l'interface Sauti"},
    {"Play / Stop","Odtwarzaj / Zatrzymaj","Lecture / Arrêt"},
    {"Previous track","Poprzedni utwór","Piste précédente"},
    {"Next track","Następny utwór","Piste suivante"},
    {"Pause / Resume","Pauza / Wznów","Pause / Reprise"},
    {"Mute","Wycisz","Muet"},
    {"Repeat","Powtarzanie","Répétition"},
    {"Shuffle","Losowanie","Aléatoire"},
    {"Reset defaults","Przywróć domyślne","Valeurs par défaut"},
    {"Keyboard shortcuts","Skróty klawiaturowe","Raccourcis clavier"},
    {"Choose one key per action. Duplicate assignments are not allowed.","Wybierz jeden klawisz dla każdej akcji. Powtórzenia są niedozwolone.","Choisissez une touche par action. Les doublons sont interdits."},
    {"Key already used by another action.","Klawisz jest już przypisany do innej akcji.","Touche déjà attribuée à une autre action."},
    {"Playback controls","Sterowanie odtwarzaniem","Commandes de lecture"},
    {"Transport and listening options","Opcje odtwarzania i odsłuchu","Options de transport et d'écoute"},
    {"Repeat & shuffle","Powtarzanie i losowanie","Répétition et lecture aléatoire"},
    {"These controls remain available on the main player, where they take effect immediately.","Te opcje pozostają dostępne w głównym oknie, gdzie działają natychmiast.","Ces commandes restent accessibles dans le lecteur principal et s'appliquent immédiatement."},
    {"About Sayari Sauti","Informacje o Sayari Sauti","À propos de Sayari Sauti"},
    {"Multiformat audio player and spectrum workstation","Wieloformatowy odtwarzacz muzyki z analizatorem widma","Lecteur audio multiformat avec analyseur de spectre"},
    {"Created by Klucznik. Sayari Sauti is developed with a strong emphasis on x86-64 assembly (NASM). Its performance-critical FFT, DSP analysis and visualizations are largely written in assembly. Portable decoders and platform integration also use C/C++ and third-party libraries.","Twórca: Klucznik. Sayari Sauti jest rozwijany z naciskiem na asembler x86-64 (NASM). Wydajnościowe elementy FFT, analizy DSP i wizualizacji są w dużej części napisane w asemblerze. Dekodery i integracja systemowa korzystają także z C/C++ i bibliotek zewnętrznych.","Créateur : Klucznik. Sayari Sauti privilégie l'assembleur x86-64 (NASM). Les composants FFT, l'analyse DSP et les visualisations sont en grande partie écrits en assembleur. Les décodeurs et l'intégration système utilisent aussi C/C++ et des bibliothèques externes."},
    {"Modern audio formats","Współczesne formaty audio","Formats audio modernes"},
    {"Retro formats and source platforms","Formaty retro i platformy pochodzenia","Formats rétro et plateformes d'origine"},
    {"Atari ST / STE: YM2, YM3, YM3b, YM5, YM6, SNDH\nAtari Falcon: AVR, DVSM (.dvs), Digital Tracker (.dtm)\nOptional codecs and variants depend on installed libraries and build configuration.","Atari ST / STE: YM2, YM3, YM3b, YM5, YM6, SNDH\nAtari Falcon: AVR, DVSM (.dvs), Digital Tracker (.dtm)\nNiektóre kodeki i warianty wymagają bibliotek opcjonalnych.","Atari ST / STE : YM2, YM3, YM3b, YM5, YM6, SNDH\nAtari Falcon : AVR, DVSM (.dvs), Digital Tracker (.dtm)\nCertains codecs et variantes dépendent de bibliothèques facultatives."},
    {"Changes to appearance and language apply after restarting Sauti.","Zmiany wyglądu głównego okna wymagają ponownego uruchomienia Sauti.","Les modifications de l'apparence du lecteur nécessitent un redémarrage de Sauti."},
    {"Close","Zamknij","Fermer"}
    };
    for(const auto &item:translations)if(strcmp(english,item.en)==0)
        return QString::fromUtf8(currentLanguage==1?item.pl:currentLanguage==2?item.fr:item.en);
    return QString::fromUtf8(english);
}
static const QColor accents[] = {"#579e91","#9a78c0","#6e9dd3"};
static const QColor backgrounds[] = {"#c9cbd3","#adc4aa","#a6bccb","#d88a3b","#52925e","#101018","#491d30","#17284a"};
static const QColor frames[] = {"#c9cbd3","#adc4aa","#88b3d3","#d88a3b","#55a75f","#695582","#963d62","#3868aa"};
static const char *names[]={"Gray","Sage","Mist blue","Amber","Forest","Original","Burgundy","Navy"};

struct State {int language=1,accent=0,background=5,frame=5;};
static QString settingsPath(){
    QString root=qEnvironmentVariable("XDG_CONFIG_HOME");
    if(root.isEmpty())root=QDir::homePath()+"/.config";
    return root+"/sayari-sauti/settings.ini";
}
static State loadState(){
    State s;QSettings ini(settingsPath(),QSettings::IniFormat);
    s.language=qBound(0,ini.value("language",s.language).toInt(),2);
    s.accent=qBound(0,ini.value("accent",s.accent).toInt(),2);
    s.background=qBound(0,ini.value("background",s.background).toInt(),7);
    s.frame=qBound(0,ini.value("frames",s.frame).toInt(),7);return s;
}
static QString defaultFolderFile(){
    return QFileInfo(settingsPath()).absolutePath()+"/default-folder";
}
static QString readDefaultFolder(){
    QFile f(defaultFolderFile());
    if(f.open(QIODevice::ReadOnly|QIODevice::Text)){
        const QString folder=QString::fromUtf8(f.readLine()).trimmed();
        if(QDir(folder).exists()&&QDir::isAbsolutePath(folder))return QDir(folder).absolutePath();
    }
    return QDir::homePath();
}
static bool writeDefaultFolder(const QString &folder){
    if(!QDir(folder).exists()||!QDir::isAbsolutePath(folder))return false;
    const QString path=defaultFolderFile();
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate|QIODevice::Text))return false;
    return f.write((QDir(folder).absolutePath()+"\\n").toUtf8())>0;
}
static void saveState(const State &s){
    QString path=settingsPath();QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Truncate|QIODevice::Text))return;
    QTextStream o(&f);
    o<<"language="<<s.language<<"\naccent="<<s.accent<<"\nbackground="<<s.background<<"\nframes="<<s.frame<<"\n";
}
class Tabs:public QWidget{
    int hovered=-1;
    double phase=0.0;
    QTimer pulse;
    std::function<void()> refresh;
    QPainterPath shapeFor(int i)const{
        const qreal widthPer=width()/6.0;
        const qreal x=i*widthPer+3;
        const qreal y=(i==current?15:19);
        const qreal base=67;
        const qreal right=x+widthPer-6;
        const qreal r=6;
        /* Exactly one pair of parallel sides: a true rounded trapezoid. */
        QPainterPath path;
        path.moveTo(x+16+r,y);
        path.lineTo(right-16-r,y);
        path.quadTo(right-16,y,right-14.5,y+r);
        path.lineTo(right,base);
        path.lineTo(x,base);
        path.lineTo(x+14.5,y+r);
        path.quadTo(x+16,y,x+16+r,y);
        path.closeSubpath();
        return path;
    }
public:
    int current=0;
    std::function<void(int)> change;
    void setBorderRefresh(std::function<void()> cb){refresh=cb;}
    QColor activeAccent()const{
        static const char *colors[]={"#4c7b89","#79658e","#917a57","#517b70","#876578","#687a94"};
        return QColor(colors[current]);
    }
    Tabs(QWidget *p=nullptr):QWidget(p){
        setFixedHeight(68);setMouseTracking(true);
        pulse.setInterval(50);
        connect(&pulse,&QTimer::timeout,this,[this](){
            phase+=0.055;
            if(phase>6.283185307)phase-=6.283185307;
            update();
            if(refresh)refresh();
        });
        pulse.start();
    }
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        static const char *colors[]={"#4c7b89","#79658e","#917a57","#517b70","#876578","#687a94"};
        static const char *labels[]={"AUDIO","APPEARANCE","LIBRARY","LANGUAGE","CONTROLS","INFO"};
        painter.setPen(Qt::NoPen);
        for(int i=0;i<6;++i){
            const bool selected=(i==current),over=(i==hovered);
            const qreal x=i*(width()/6.0)+3;
            const qreal top=selected?15:19;
            QColor accent(colors[i]);
            const int breathe=selected?(int)(8.0+6.0*std::sin(phase)):0;
            QColor high=accent.lighter(selected?155:over?140:110);
            QColor low=QColor("#202633");
            high.setAlpha(selected?125+breathe:over?84:57);
            low.setAlpha(245);
            QLinearGradient fill(x,top,x,67);
            fill.setColorAt(0.0,high);
            fill.setColorAt(0.52,QColor(selected?"#353949":"#272a38"));
            fill.setColorAt(1.0,low);
            const auto shape=shapeFor(i);
            painter.fillPath(shape,fill);
            QColor outline=accent.lighter(selected?157:over?135:100);
            outline.setAlpha(selected?180+breathe:over?145:90);
            painter.setPen(QPen(outline,selected?1.45:1.0));
            painter.drawPath(shape);
            if(selected){
                painter.setPen(Qt::NoPen);
                QColor glow=accent.lighter(165);glow.setAlpha(90+breathe*2);
                painter.fillRect(QRectF(x+16,65,width()/6.0-38,2),glow);
            }
            painter.setPen(selected?QColor("#f2f5fb"):over?QColor("#e6e9f0"):QColor("#acb4c5"));
            QFont f=painter.font();f.setPixelSize(11);
            f.setWeight(selected?QFont::DemiBold:QFont::Medium);painter.setFont(f);
            painter.drawText(QRectF(x+4,top+6,width()/6.0-14,29),Qt::AlignCenter,L(labels[i]));
        }
    }
    void mouseMoveEvent(QMouseEvent *e)override{
        int candidate=qBound(0,(int)(e->position().x()/(width()/6.0)),5);
        if(!shapeFor(candidate).contains(e->position()))candidate=-1;
        if(candidate!=hovered){hovered=candidate;update();}
        QWidget::mouseMoveEvent(e);
    }
    void leaveEvent(QEvent *e)override{hovered=-1;update();QWidget::leaveEvent(e);}
    void mousePressEvent(QMouseEvent *e)override{
        int i=qBound(0,(int)(e->position().x()/(width()/6.0)),5);
        if(!shapeFor(i).contains(e->position()))return;
        if(current!=i){current=i;phase=0;update();if(refresh)refresh();if(change)change(i);}
    }
};
/* One continuous frame connects the content with the selected trapezoid. */
class TabContentFrame:public QWidget{
    Tabs *tabs;
public:
    explicit TabContentFrame(Tabs *t,QWidget *p=nullptr):QWidget(p),tabs(t){
        tabs->setBorderRefresh([this](){update();});
    }
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        QRectF rect=QRectF(0.8,0.8,width()-1.6,height()-1.6);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#171c28"));
        p.drawRoundedRect(rect,10,10);
        QColor edge("#364354");p.setPen(QPen(edge,1));
        QPainterPath frame;
        frame.moveTo(0.8,0.8);frame.lineTo(0.8,height()-11);
        frame.quadTo(0.8,height()-0.8,11,height()-0.8);
        frame.lineTo(width()-11,height()-0.8);
        frame.quadTo(width()-0.8,height()-0.8,width()-0.8,height()-11);
        frame.lineTo(width()-0.8,11);
        frame.quadTo(width()-0.8,0.8,width()-11,0.8);
        const qreal w=width()/6.0;
        const qreal cutL=tabs->current*w+4;
        const qreal cutR=(tabs->current+1)*w-4;
        frame.moveTo(width()-11,0.8);frame.lineTo(cutR,0.8);
        frame.moveTo(cutL,0.8);frame.lineTo(11,0.8);
        frame.quadTo(0.8,0.8,0.8,11);
        p.drawPath(frame);
        QColor accent=tabs->activeAccent();accent.setAlpha(165);
        p.setPen(QPen(accent,1.5));
        p.drawLine(QPointF(cutL,0.8),QPointF(cutL+20,0.8));
        p.drawLine(QPointF(cutR-20,0.8),QPointF(cutR,0.8));
    }
};
/* Color selectors are swatches, not conventional command buttons. */
class Swatch:public QWidget{
    QColor color;QString title;bool selected=false,hovered=false;
public:
    std::function<void()> activated;
    Swatch(QColor c,QString name,bool initial,QWidget *parent=nullptr):
      QWidget(parent),color(c),title(name),selected(initial){
        setMinimumSize(62,76);setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover,true);setToolTip(name);
    }
    void select(bool value){selected=value;update();}
    void setLocalizedTitle(const QString &value){title=value;setToolTip(value);update();}
protected:
    void enterEvent(QEnterEvent *e)override{hovered=true;update();QWidget::enterEvent(e);}
    void leaveEvent(QEvent *e)override{hovered=false;update();QWidget::leaveEvent(e);}
    void mousePressEvent(QMouseEvent *e)override{
        if(e->button()==Qt::LeftButton&&activated)activated();
    }
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const QRectF sample(7,7,width()-14,39);
        QLinearGradient gradient(sample.topLeft(),sample.bottomLeft());
        gradient.setColorAt(0,color.lighter(120));gradient.setColorAt(1,color.darker(112));
        p.setPen(QPen(selected?QColor("#d5e7ed"):hovered?QColor("#8195ad"):QColor("#4c586b"),selected?2:1));
        p.setBrush(gradient);p.drawRoundedRect(sample,9,9);
        if(selected){
            p.setPen(QPen(QColor("#f1f8fa"),2));
            p.drawLine(QPointF(sample.right()-20,sample.center().y()),
                       QPointF(sample.right()-15,sample.center().y()+5));
            p.drawLine(QPointF(sample.right()-15,sample.center().y()+5),
                       QPointF(sample.right()-7,sample.center().y()-6));
        }
        p.setPen(selected?QColor("#f0f4fa"):QColor("#aeb8ca"));
        QFont font=p.font();font.setPixelSize(11);font.setWeight(selected?QFont::DemiBold:QFont::Normal);
        p.setFont(font);p.drawText(QRectF(0,52,width(),21),Qt::AlignCenter,title);
    }
};
static QLabel *localizedLabel(const char *key){
    auto *label=new QLabel(L(key));
    label->setProperty("translationKey",QString::fromUtf8(key));
    return label;
}
static QLabel *heading(const char *key){
    auto *label=localizedLabel(key);label->setObjectName("heading");return label;
}
static QFrame *panel(QVBoxLayout *parent){
    auto *f=new QFrame;f->setObjectName("panel");
    auto *v=new QVBoxLayout(f);v->setContentsMargins(22,20,22,20);v->setSpacing(14);
    parent->addWidget(f);return f;
}
static QVBoxLayout *panelLayout(QFrame *f){return qobject_cast<QVBoxLayout*>(f->layout());}
class Settings:public QWidget{
    State state;QStackedWidget *pages;Tabs *tabs;
    void refreshLanguage(){
        currentLanguage=state.language;
        setWindowTitle(state.language==1?"Sayari Sauti | Ustawienia":
            state.language==2?"Sayari Sauti | Préférences":"Sayari Sauti | Settings");
        for(auto *label:findChildren<QLabel*>()){
            const QString key=label->property("translationKey").toString();
            if(!key.isEmpty())label->setText(L(key.toUtf8().constData()));
        }
        for(auto *object:findChildren<QWidget*>()){
            auto *swatch=dynamic_cast<Swatch*>(object);
            if(!swatch)continue;
            const QString key=swatch->property("translationKey").toString();
            if(!key.isEmpty())swatch->setLocalizedTitle(L(key.toUtf8().constData()));
        }
        for(auto *combo:findChildren<QComboBox*>()){
            const QString key=combo->property("translationKey").toString();
            if(!key.isEmpty()&&combo->count()>0)combo->setItemText(0,L(key.toUtf8().constData()));
        }
        for(auto *button:findChildren<QPushButton*>()){
            const QString key=button->property("translationKey").toString();
            if(!key.isEmpty())button->setText(L(key.toUtf8().constData()));
        }
        tabs->update();
        updateGeometry();
    }
    void putPage(int idx,QWidget *page){pages->insertWidget(idx,page);}
    QWidget *makePage(const QString &title,const QString &subtitle){
        auto *page=new QWidget;auto *v=new QVBoxLayout(page);v->setContentsMargins(10,16,10,10);v->setSpacing(16);
        v->addWidget(heading(title.toUtf8().constData()));auto *desc=localizedLabel(subtitle.toUtf8().constData());desc->setObjectName("muted");v->addWidget(desc);return page;
    }
    void addPalette(QVBoxLayout *v,const QString &label,const QColor *colors,int selected,std::function<void(int)> change){
        auto *f=panel(v);auto *layout=panelLayout(f);layout->addWidget(heading(label.toUtf8().constData()));
        auto *row=new QHBoxLayout;row->setSpacing(5);
        auto *group=new std::vector<Swatch*>;
        for(int i=0;i<8;i++){
            auto *swatch=new Swatch(colors[i],L(names[i]),i==selected);
            swatch->setProperty("translationKey",QString::fromUtf8(names[i]));
            row->addWidget(swatch,1);group->push_back(swatch);
            swatch->activated=[this,change,i,group](){
                change(i);
                for(size_t j=0;j<group->size();j++)(*group)[j]->select((int)j==i);
                saveState(state);
            };
        }
        /* QObject owns the swatches; the pointer collection follows the panel. */
        connect(f,&QObject::destroyed,this,[group](){delete group;});
        layout->addLayout(row);
    }
public:
    Settings(){
        state=loadState();currentLanguage=state.language;setWindowTitle(currentLanguage==1?"Sayari Sauti | Ustawienia":currentLanguage==2?"Sayari Sauti | Préférences":"Sayari Sauti | Settings");
        resize(980,740);setMinimumSize(930,690);
        setStyleSheet("QWidget{background:#131722;color:#e6eaf3;font-family:'Noto Sans',sans-serif;font-size:13px;}"
         "QLabel#heading{font-size:16px;font-weight:600;color:#f2f4f9;}"
         "QLabel#muted{color:#969fb3;font-size:12px;}"
         "QFrame#panel{background:#1e2431;border:1px solid #333d50;border-radius:12px;}"
         "QFrame#panel QLabel{background:transparent;}"
         "QPushButton{border:1px solid #47536a;background:#2c3547;border-radius:8px;padding:12px;color:#e2e8f2;}"
         "QPushButton:hover{background:#3b465d;}"
         "QComboBox{background:#2d3649;border:1px solid #59667c;border-radius:7px;padding:11px;}"
         "QRadioButton{background:transparent;padding:8px;}");
        auto *root=new QVBoxLayout(this);root->setContentsMargins(28,20,28,20);root->setSpacing(0);
        auto *top=new QHBoxLayout;auto *name=heading("SAYARI  /  SAUTI");top->addWidget(name);top->addStretch();
        auto *subtitle=localizedLabel("PREFERENCES  ·  AUDIO WORKSTATION");subtitle->setObjectName("muted");top->addWidget(subtitle);
        root->addLayout(top);root->addSpacing(18);
        tabs=new Tabs;root->addWidget(tabs);
        auto *contentFrame=new TabContentFrame(tabs);
        auto *frameLayout=new QVBoxLayout(contentFrame);
        frameLayout->setContentsMargins(14,7,14,10);
        pages=new QStackedWidget;
        frameLayout->addWidget(pages);
        root->addWidget(contentFrame,1);
        tabs->change=[this](int i){pages->setCurrentIndex(i);};
        {
            auto *pg=makePage("Audio output","The active playback path and sample format");
            auto *v=qobject_cast<QVBoxLayout*>(pg->layout());
            auto *f=panel(v);auto *p=panelLayout(f);p->addWidget(heading("Playback backend"));
            auto *c=new QComboBox;c->addItem(L("SDL2 / Float32 — active"));c->setProperty("translationKey",QString::fromUtf8("SDL2 / Float32 — active"));c->setEnabled(false);p->addWidget(c);
            auto *hint=localizedLabel("Native PipeWire playback and backend switching are in development.");
            hint->setObjectName("muted");p->addWidget(hint);
            auto *f2=panel(v);auto *p2=panelLayout(f2);p2->addWidget(heading("Signal path"));
            p2->addWidget(localizedLabel("Decoder → Float32 PCM → SDL2 → system audio"));
            auto *h=localizedLabel("Source sample rate is requested; final hardware format depends on PipeWire.");
            h->setObjectName("muted");p2->addWidget(h);v->addStretch();putPage(0,pg);
        }
        {
            auto *pg=makePage("Appearance","Personalize the spectrum workspace without affecting audio");
            auto *v=qobject_cast<QVBoxLayout*>(pg->layout());
            auto *f=panel(v);auto *p=panelLayout(f);p->addWidget(heading("Accent"));
            auto *row=new QHBoxLayout;row->setSpacing(18);
            const char *labels[]={"Emerald","Violet","Blue"};
            auto *choices=new std::vector<Swatch*>;
            for(int i=0;i<3;i++){
                auto *swatch=new Swatch(accents[i],L(labels[i]),state.accent==i);
                swatch->setFixedSize(102,68);
                swatch->setProperty("translationKey",QString::fromUtf8(labels[i]));
                row->addWidget(swatch,0);choices->push_back(swatch);
                swatch->activated=[this,i,choices](){
                    state.accent=i;saveState(state);
                    for(size_t j=0;j<choices->size();j++)(*choices)[j]->select((int)j==i);
                };
            }
            connect(f,&QObject::destroyed,this,[choices](){delete choices;});
            row->addStretch();
            p->addLayout(row);
            addPalette(v,"Workspace background",backgrounds,state.background,[this](int i){state.background=i;});
            addPalette(v,"Frame color",frames,state.frame,[this](int i){state.frame=i;});
            v->addStretch();putPage(1,pg);
        }
        {
            auto *pg=makePage("Music library","File locations and browsing");
            auto *v=qobject_cast<QVBoxLayout*>(pg->layout());
            auto *f=panel(v);auto *p=panelLayout(f);
            p->addWidget(heading("Default music folder"));
            auto *pathLabel=localizedLabel("Current directory");
            pathLabel->setObjectName("muted");
            p->addWidget(pathLabel);
            auto *folder=new QLabel(readDefaultFolder());
            folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
            folder->setWordWrap(true);
            folder->setStyleSheet("background:#273144;border:1px solid #44516a;border-radius:8px;padding:12px;color:#f0f4fa;");
            p->addWidget(folder);
            auto *buttons=new QHBoxLayout;
            auto *choose=new QPushButton(L("Choose folder…"));
            choose->setProperty("translationKey",QString::fromUtf8("Choose folder…"));
            auto *save=new QPushButton(L("Save as default"));
            save->setProperty("translationKey",QString::fromUtf8("Save as default"));
            save->setEnabled(false);
            buttons->addWidget(choose);buttons->addWidget(save);buttons->addStretch();
            p->addLayout(buttons);
            auto *message=localizedLabel("The chosen location becomes the starting folder in Sauti.");
            message->setObjectName("muted");p->addWidget(message);
            connect(choose,&QPushButton::clicked,this,[this,folder,save](){
                const QString next=QFileDialog::getExistingDirectory(this,L("Choose music folder"),folder->text(),QFileDialog::ShowDirsOnly);
                if(next.isEmpty())return;
                folder->setText(QDir(next).absolutePath());
                save->setEnabled(true);
            });
            connect(save,&QPushButton::clicked,this,[this,folder,save,message](){
                if(writeDefaultFolder(folder->text())){
                    save->setEnabled(false);
                    message->setText(L("Folder saved successfully."));
                    message->setProperty("translationKey",QString::fromUtf8("Folder saved successfully."));
                }else{
                    message->setText(L("Could not save the folder."));
                    message->setProperty("translationKey",QString::fromUtf8("Could not save the folder."));
                }
            });
            v->addStretch();putPage(2,pg);
        }
        {
            auto *pg=makePage("Language","Select the language used by Sauti");
            auto *v=qobject_cast<QVBoxLayout*>(pg->layout());
            auto *f=panel(v);auto *p=panelLayout(f);
            auto *en=new QRadioButton("English"),*pl=new QRadioButton("Polski"),*fr=new QRadioButton("Français");en->setChecked(state.language==0);pl->setChecked(state.language==1);fr->setChecked(state.language==2);
            p->addWidget(en);p->addWidget(pl);p->addWidget(fr);
            auto changeLanguage=[this](int nextLanguage){
                if(state.language==nextLanguage)return;
                state.language=nextLanguage;
                saveState(state);
                refreshLanguage(); // same Qt window, no close or new Settings
            };
            connect(en,&QRadioButton::clicked,this,[changeLanguage](){changeLanguage(0);});
            connect(pl,&QRadioButton::clicked,this,[changeLanguage](){changeLanguage(1);});
            connect(fr,&QRadioButton::clicked,this,[changeLanguage](){changeLanguage(2);});
            v->addStretch();putPage(3,pg);
        }
        {
            auto *pg=makePage("Playback controls","Transport and listening options");
            auto *v=qobject_cast<QVBoxLayout*>(pg->layout());
            auto *f=panel(v);auto *p=panelLayout(f);
            p->addWidget(heading("Keyboard shortcuts"));
            auto *help=localizedLabel("Choose one key per action. Duplicate assignments are not allowed.");
            help->setObjectName("muted");p->addWidget(help);
            static const char *keys[]={"Space","Left","Right","P","M","R","H","S","A","B","C","D","E","F","G","I","J","K","L","N","O","Q","T","U","V","W","X","Y","Z","Up","Down"};
            static const char *ids[]={"play_stop","previous","next","pause","mute","repeat","shuffle"};
            static const char *labels[]={"Play / Stop","Previous track","Next track","Pause / Resume","Mute","Repeat","Shuffle"};
            static const char *defaults[]={"Space","Left","Right","P","M","R","H"};
            auto *grid=new QGridLayout;
            std::vector<QComboBox*> combos;
            QSettings shortcuts(QFileInfo(settingsPath()).absolutePath()+"/shortcuts.ini",QSettings::IniFormat);
            for(int i=0;i<7;i++){
                auto *label=localizedLabel(labels[i]);grid->addWidget(label,i,0);
                auto *combo=new QComboBox;combo->setMinimumWidth(180);
                for(auto key:keys)combo->addItem(key);
                QString selected=shortcuts.value(ids[i],defaults[i]).toString();
                if(combo->findText(selected)<0)selected=defaults[i];
                combo->setCurrentText(selected);
                combo->setProperty("shortcutId",QString::fromLatin1(ids[i]));
                grid->addWidget(combo,i,1);combos.push_back(combo);
            }
            p->addLayout(grid);
            auto *error=localizedLabel("Key already used by another action.");
            error->setObjectName("muted");error->hide();p->addWidget(error);
            auto *reset=new QPushButton(L("Reset defaults"));
            reset->setProperty("translationKey",QString::fromUtf8("Reset defaults"));
            p->addWidget(reset);
            auto all=std::make_shared<std::vector<QComboBox*>>(combos);
            for(int i=0;i<7;i++){
                connect((*all)[i],&QComboBox::currentTextChanged,this,
                    [this,all,error,i](const QString &key){
                        if(key.isEmpty())return;
                        for(int j=0;j<7;j++)if(j!=i && (*all)[j]->currentText()==key){
                            error->show();
                            auto *source=qobject_cast<QComboBox*>(sender());
                            if(source){
                                QSignalBlocker block(source);
                                QSettings conf(QFileInfo(settingsPath()).absolutePath()+"/shortcuts.ini",QSettings::IniFormat);
                                const QString id=source->property("shortcutId").toString();
                                static const char *defs[]={"Space","Left","Right","P","M","R","H"};
                                source->setCurrentText(conf.value(id,defs[i]).toString());
                            }
                            return;
                        }
                        error->hide();
                        QSettings conf(QFileInfo(settingsPath()).absolutePath()+"/shortcuts.ini",QSettings::IniFormat);
                        conf.setValue((*all)[i]->property("shortcutId").toString(),key);
                        conf.sync();
                    });
            }
            connect(reset,&QPushButton::clicked,this,[all,error](){
                static const char *defaults[]={"Space","Left","Right","P","M","R","H"};
                QSettings conf(QFileInfo(settingsPath()).absolutePath()+"/shortcuts.ini",QSettings::IniFormat);
                for(int i=0;i<7;i++){
                    QSignalBlocker blocker((*all)[i]);
                    (*all)[i]->setCurrentText(defaults[i]);
                    conf.setValue((*all)[i]->property("shortcutId").toString(),defaults[i]);
                }
                conf.sync();error->hide();
            });
            v->addStretch();putPage(4,pg);
        }
        {
            auto *pg=makePage("About Sayari Sauti","Multiformat audio player and spectrum workstation");
            auto *v=qobject_cast<QVBoxLayout*>(pg->layout());
            auto *f=panel(v);auto *p=panelLayout(f);
            p->addWidget(heading("SAYARI SAUTI"));
            auto *about=localizedLabel("Created by Klucznik. Sayari Sauti is developed with a strong emphasis on x86-64 assembly (NASM). Its performance-critical FFT, DSP analysis and visualizations are largely written in assembly. Portable decoders and platform integration also use C/C++ and third-party libraries.");
            about->setWordWrap(true);p->addWidget(about);
            auto *f2=panel(v);auto *p2=panelLayout(f2);
            p2->addWidget(heading("Modern audio formats"));
            auto *modern=localizedLabel("WAV / PCM  ·  MP3  ·  FLAC  ·  Ogg Vorbis  ·  Opus  ·  MIDI  ·  WavPack (.wv)");
            modern->setWordWrap(true);p2->addWidget(modern);
            auto *f3=panel(v);auto *p3=panelLayout(f3);
            p3->addWidget(heading("Retro formats and source platforms"));
            auto *retro=localizedLabel("Atari ST / STE: YM2, YM3, YM3b, YM5, YM6, SNDH\\nAtari Falcon: AVR, DVSM (.dvs), Digital Tracker (.dtm)\\nOptional codecs and variants depend on installed libraries and build configuration.");
            retro->setWordWrap(true);p3->addWidget(retro);
            v->addStretch();putPage(5,pg);
        }
        auto *foot=new QHBoxLayout;auto *note=localizedLabel("Changes to appearance and language apply after restarting Sauti.");
        note->setObjectName("muted");foot->addWidget(note);foot->addStretch();
        auto *close=new QPushButton(L("Close"));close->setProperty("translationKey",QString::fromUtf8("Close"));close->setFixedWidth(110);foot->addWidget(close);connect(close,&QPushButton::clicked,this,&QWidget::close);
        root->addSpacing(12);root->addLayout(foot);
    }
};
int main(int argc,char **argv){
    QApplication a(argc,argv);
    auto *w=new Settings;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
    return a.exec();
}
