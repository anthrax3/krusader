/***************************************************************************
                                 krslots.cpp
                             -------------------
    copyright            : (C) 2001 by Shie Erlich & Rafi Yanai
    email                : krusader@users.sourceforge.net
    web site		 : http://krusader.sourceforge.net
 ---------------------------------------------------------------------------
  Description
 ***************************************************************************

  A

     db   dD d8888b. db    db .d8888.  .d8b.  d8888b. d88888b d8888b.
     88 ,8P' 88  `8D 88    88 88'  YP d8' `8b 88  `8D 88'     88  `8D
     88,8P   88oobY' 88    88 `8bo.   88ooo88 88   88 88ooooo 88oobY'
     88`8b   88`8b   88    88   `Y8b. 88~~~88 88   88 88~~~~~ 88`8b
     88 `88. 88 `88. 88b  d88 db   8D 88   88 88  .8D 88.     88 `88.
     YP   YD 88   YD ~Y8888P' `8888Y' YP   YP Y8888D' Y88888P 88   YD

                                                     S o u r c e    F i l e

 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <qdir.h>
#include <qpoint.h>
#include <qstringlist.h>
#include <qprogressdialog.h>
#include <qlistview.h>
#include <qpixmapcache.h>
// KDE includes
#include <klocale.h>
#include <kprocess.h>
#include <kmessagebox.h>
#include <klineeditdlg.h>
#include <kkeydialog.h>
#include <kdirnotify_stub.h>
#include <kio/netaccess.h>
#include <kedittoolbar.h>
#include <kdeversion.h>
#include <kcmdlineargs.h>
#include <KViewer/krviewer.h>
// Krusader includes
#include "krslots.h"
#include "krusader.h"
#include "krusaderview.h"
#include "Panel/listpanel.h"
#include "Dialogs/krdialogs.h"
#include "Dialogs/krspwidgets.h"
#include "GUI/krusaderstatus.h"
#include "RemoteMan/remoteman.h"
#include "Panel/panelfunc.h"
#include "Konfigurator/konfigurator.h"
#include "MountMan/kmountman.h"
#include "defaults.h"
#include "resources.h"
#include "GUI/kfnkeys.h"
#include "GUI/kcmdline.h"
#include "Search/krquery.h"
#include "Search/krsearchmod.h"
#include "Search/krsearchdialog.h"
#include "Locate/locate.h"
#include "VFS/vfs.h"
#include "panelmanager.h"
#include "Splitter/splittergui.h"
#include "Splitter/splitter.h"
#include "Splitter/combiner.h"
#include "UserMenu/usermenu.h"
#include "Panel/panelpopup.h"
#include "Synchronizer/synchronizergui.h"
#include "krservices.h"
#include "GUI/profilemanager.h"

#define REFRESH_BOTH_PANELS { ListPanel *p=ACTIVE_PANEL;        \
                              MAIN_VIEW->left->func->refresh(); \
	                            MAIN_VIEW->right->func->refresh();\
                              p->slotFocusOnMe(); }
#define ACTIVE_PANEL_MANAGER  (ACTIVE_PANEL == krApp->mainView->left ? krApp->mainView->leftMng : \
                                krApp->mainView->rightMng)

void KRslots::sendFileByEmail(QString filename) {
  QString mailProg;
  QStringList lst = Krusader::supportedTools();
  if (lst.contains("MAIL")) mailProg=lst[lst.findIndex("MAIL") + 1];
  else {
    KMessageBox::error(0,i18n("Krusader can't find any of the supported mail clients. Please install one to your path. Hint: Krusader supports kmail."));
    return;
  }

  KShellProcess proc;

  if ( vfs::fromPathOrURL( mailProg ).fileName() == "kmail") {
    proc << "kmail" << "--subject \""+i18n("Sending file: ")+
            filename.mid(filename.findRev('/')+1)+"\"" << QString::null +
            "--attach "+"\"" + filename + "\"";
  }

	if (!proc.start(KProcess::DontCare))
    KMessageBox::error(0,i18n("Error executing ")+mailProg+" !");
  else proc.detach();
}

void KRslots::compareContent() {
  QStringList lstLeft, lstRight;
  QStringList* lstActive;
  KURL name1, name2;

  MAIN_VIEW->left->getSelectedNames( &lstLeft );
  MAIN_VIEW->right->getSelectedNames( &lstRight );
  lstActive = ( ACTIVE_PANEL->isLeft() ? &lstLeft : &lstRight );
  
  if ( lstLeft.count() == 1 && lstRight.count() == 1 ) {
    // first, see if we've got exactly 1 selected file in each panel:
    name1 = MAIN_VIEW->left->func->files()->vfs_getFile( lstLeft[0] );
    name2 = MAIN_VIEW->right->func->files()->vfs_getFile( lstRight[0] );
  }
  else if ( lstActive->count() == 2 ) {
    // next try: are in the current panel exacty 2 files selected?
    name1 = ACTIVE_PANEL->func->files()->vfs_getFile( (*lstActive)[0] );
    name2 = ACTIVE_PANEL->func->files()->vfs_getFile( (*lstActive)[1] );
  }
  else if ( ACTIVE_PANEL->otherPanel->func->files()->vfs_search( ACTIVE_PANEL->view->getCurrentItem() ) ) {
    // next try: is in the other panel a file with the same name?
    name1 = ACTIVE_PANEL->func->files()->vfs_getFile( ACTIVE_PANEL->view->getCurrentItem() );
    name2 = ACTIVE_PANEL->otherPanel->func->files()->vfs_getFile( ACTIVE_PANEL->view->getCurrentItem() );
  }
  else  {
    // if we got here, then we can't be sure what file to diff
    KMessageBox::detailedError(0,i18n("Don't know which files to compare."),
      i18n("To compare 2 files by content, you can either\n"
		"- select (mark) one file in the left panel, and one in the right panel\n"
		"- select exactly two files in the active panel\n"
		"- make sure in the other panel is a file with the same name then the current in the active panel"));
    return;
  }

  // else implied: all ok, let's call kdiff
  // but if one of the files isn't local, download them first
  compareContent( name1, name2 );
}

class KrProcess: public KProcess
{
  QString tmp1, tmp2;
  
public:
  KrProcess( QString in1, QString in2 )
  {
    tmp1 = in1;
    tmp2 = in2;
  }
  
  virtual void processHasExited (int )
  {
    if( !tmp1.isEmpty() )
      KIO::NetAccess::removeTempFile( tmp1 );
    if( !tmp2.isEmpty() )
      KIO::NetAccess::removeTempFile( tmp2 );
    delete this;
  }
};

void KRslots::compareContent( KURL url1, KURL url2 )
{
  QString diffProg;
  QStringList lst = Krusader::supportedTools();
  if (lst.contains("DIFF")) diffProg=lst[lst.findIndex("DIFF") + 1];
  else {
    KMessageBox::error(0,i18n("Krusader can't find any of the supported diff-frontends. Please install one to your path. Hint: Krusader supports kdiff and xxdiff."));
    return;
  }

  QString tmp1 = QString::null, tmp2 = QString::null;
  
  if (!url1.isLocalFile()) {
    if( !KIO::NetAccess::download( url1, tmp1, 0 ) ){
      KMessageBox::sorry(krApp,i18n("Krusader is unable to download: ")+url1.fileName());
      return;
    }
  } else tmp1 = url1.path();
  if (!url2.isLocalFile()) {
    if( !KIO::NetAccess::download( url2, tmp2, 0 ) ){
      KMessageBox::sorry(krApp,i18n("Krusader is unable to download: ")+url2.fileName());
      if( tmp1 != url1.path() )
        KIO::NetAccess::removeTempFile( tmp1 );
      return;
    }
  } else tmp2 = url2.path();

  KrProcess *p = new KrProcess( tmp1 != url1.path() ? tmp1 : QString::null,
                                tmp2 != url2.path() ? tmp2 : QString::null );
  *p << diffProg << tmp1 << tmp2;
  if (!p->start(KProcess::DontCare))
    KMessageBox::error(0,i18n("Error executing ")+diffProg+" !");
}

void KRslots::rightclickMenu() {
  ACTIVE_PANEL->popRightClickMenu(
   ACTIVE_PANEL->mapToGlobal(
   dynamic_cast<KListView*>(ACTIVE_PANEL->view)->itemRect(dynamic_cast<QListViewItem*>
   (ACTIVE_PANEL->view->getCurrentKrViewItem())).topLeft()
   )
  );
}

void KRslots::addBookmark(){
  QString path=ACTIVE_PANEL->virtualPath;
  if (path.contains('\\')>0)
    KMessageBox::information(0, i18n("In order to save the time needed to enter an archive, Krusader will not allow bookmarks which point into an archive, as it is usually a mistake to do so!"),
                             QString::null, "BookmarkArchives");
}

// GUI toggle slots
void KRslots::toggleFnkeys(){
  if( MAIN_VIEW->fnKeys->isVisible() ) MAIN_VIEW->fnKeys->hide();
  else MAIN_VIEW->fnKeys->show();
}

void KRslots::toggleCmdline(){
  if( MAIN_VIEW->cmdLine->isVisible() ) MAIN_VIEW->cmdLine->hide();
  else MAIN_VIEW->cmdLine->show();
}

void KRslots::toggleToolbar() {
  if (krApp->toolBar()->isVisible())
		krApp->toolBar()->hide();
	else krApp->toolBar()->show();
}

void KRslots::toggleStatusbar() {
  if (krApp->statusBar()->isVisible())
		krApp->statusBar()->hide();
	else krApp->statusBar()->show();
}

void KRslots::toggleTerminal() {
  if( MAIN_VIEW->terminal_dock->isVisible() ) MAIN_VIEW->slotTerminalEmulator(false);
  else MAIN_VIEW->slotTerminalEmulator(true);
}

void KRslots::showAboutApplication() {
  KRAbout *q=new KRAbout();
  q->exec();
}

void KRslots::insertFileName(bool full_path)
{
  QString filename = ACTIVE_PANEL->view->getCurrentItem();
  if( filename == QString::null )
    return;
  
  if( full_path ){
    QString path=ACTIVE_FUNC->files()->vfs_getOrigin().prettyURL(1,KURL::StripFileProtocol);
    filename = path+filename;
  }

  QString current = MAIN_VIEW->cmdLine->text();
  if( current.length() != 0 && !current.endsWith( " " ) )
    current += " ";
  MAIN_VIEW->cmdLine->setText( current + filename );
}

// directory list functions
void KRslots::allFilter()			  {	ACTIVE_PANEL->setFilter(KrView::All);	     }
void KRslots::execFilter()			{	ACTIVE_PANEL->setFilter(KrView::All);	     }
void KRslots::customFilter()		{	ACTIVE_PANEL->setFilter(KrView::Custom);   }
void KRslots::markAll()         { ACTIVE_PANEL->select(true,true);           }
void KRslots::unmarkAll()       { ACTIVE_PANEL->select(false,true);          }
void KRslots::markGroup()       { ACTIVE_PANEL->select(true,false);          }
void KRslots::markGroup(const QString& mask, bool select) { ACTIVE_PANEL->select(mask, select); }
void KRslots::unmarkGroup()     { ACTIVE_PANEL->select(false,false);         }
void KRslots::invert()          { ACTIVE_PANEL->invertSelection();           }

void KRslots::root()            { ACTIVE_FUNC->openUrl(vfs::fromPathOrURL("/"));}
void KRslots::refresh(const KURL& u){ ACTIVE_FUNC->delayedOpenUrl(u);        }
void KRslots::home()            { ACTIVE_FUNC->openUrl(QDir::homeDirPath()); }
void KRslots::refresh()         { ACTIVE_FUNC->refresh();                    }
void KRslots::properties()      { ACTIVE_FUNC->properties();                 }
void KRslots::dirUp()           { ACTIVE_FUNC->dirUp();                      }
void KRslots::back()            { ACTIVE_FUNC->goBack();                     }
void KRslots::slotPack()        { ACTIVE_FUNC->pack();                       }
void KRslots::slotUnpack()      { ACTIVE_FUNC->unpack();                     }
void KRslots::testArchive()     { ACTIVE_FUNC->testArchive();                }
void KRslots::calcSpace()       { ACTIVE_FUNC->calcSpace();                  }
void KRslots::FTPDisconnect()   { ACTIVE_FUNC->FTPDisconnect();              }
void KRslots::newFTPconnection(){ ACTIVE_FUNC->newFTPconnection();           }

// run external modules / programs
void KRslots::runKonfigurator(bool firstTime) {

  krConfig->setGroup( "Look&Feel" );
  int size = (krConfig->readEntry("Filelist Icon Size",_FilelistIconSize)).toInt();
  
  Konfigurator *konfigurator = new Konfigurator(firstTime);

  if( konfigurator->isGUIRestartNeeded() )
  {
    krConfig->setGroup( "Look&Feel" );
    if((krConfig->readEntry("Filelist Icon Size",_FilelistIconSize)).toInt() != size )
      QPixmapCache::clear();

    MAIN_VIEW->leftMng->recreatePanels();
    MAIN_VIEW->rightMng->recreatePanels();
  }
  
  delete konfigurator;
}

void KRslots::toggleHidden(){
  krConfig->setGroup("Look&Feel");
	bool show = !krConfig->readBoolEntry("Show Hidden",_ShowHidden);
	krApp->actToggleHidden->setChecked(show);
  krConfig->writeEntry("Show Hidden",show);
	REFRESH_BOTH_PANELS;
}

void KRslots::toggleSwapPanels(){
  MAIN_VIEW->horiz_splitter->moveToLast( MAIN_VIEW->leftMng );

  ListPanel     *tmpPanel;     // temporary variables for swapping
  PanelManager  *tmpMng;

  tmpMng = MAIN_VIEW->leftMng;
  MAIN_VIEW->leftMng = MAIN_VIEW->rightMng;
  MAIN_VIEW->rightMng = tmpMng;
    
  tmpPanel = MAIN_VIEW->left;
  MAIN_VIEW->left = MAIN_VIEW->right;
  MAIN_VIEW->right = tmpPanel;
  
  MAIN_VIEW->leftMng->swapPanels();
  MAIN_VIEW->rightMng->swapPanels();
  
  MAIN_VIEW->left->updateGeometry();
  MAIN_VIEW->right->updateGeometry();
}

void KRslots::search() {
  new KrSearchDialog();
}

void KRslots::locate()
{
  if( !KrServices::cmdExist( "locate" ) )
  {
    KMessageBox::error(krApp, i18n( "Can't find the 'locate' command. Please install the "
                                    "findutils-locate package of GNU, or set its dependencies in "
                                    "Konfigurator" ));
    return;
  }
  
  LocateDlg *loc = new LocateDlg();
  delete loc;
}

void KRslots::runRemoteMan() {
  // display outage information
  KMessageBox::information(krApp, i18n( "Important: RemoteMan has been replaced by our new bookmark manager."
                                        "The new manager handles local files and remote url the same way."
                                        "RemoteMan is being left around to allow an easier transition and"
                                        "give you a chance to move your bookmarks. IT WILL BE REMOVED SOON!"
                                        "\n"
                                        "Try the new bookmark-manager: open a new remote connection. once"
                                        "done, press the bookmark button, select 'Add bookmark' and that's it!"));

  QString host=remoteMan::getHost();
	if (host==QString::null) return;
	// otherwise, attempt a connection
	ACTIVE_FUNC->openUrl(vfs::fromPathOrURL(host));
}

void KRslots::runMountMan() {
  // left as a precaution, although we use kde's services now
  if( !KrServices::cmdExist( "df" ) || !KrServices::cmdExist( "mount" ) )
  {
    KMessageBox::error(0,
      i18n("Can't start 'mount'! Check the 'Dependencies' page in konfigurator."));
    return;
  }
  
  krApp->mountMan->mainWindow();
}

void KRslots::homeTerminal(){
  QString save = getcwd(0,0);
  chdir (QDir::homeDirPath().local8Bit());

  KProcess proc;
  krConfig->setGroup("General");
  QString term = krConfig->readEntry("Terminal",_Terminal);
  proc << KrServices::separateArgs( term );
      
  if( term.contains( "konsole" ) )   /* KDE 3.2 bug (konsole is killed by pressing Ctrl+C) */
  {                                  /* Please remove the patch if the bug is corrected */
    proc << "&";
    proc.setUseShell( true );
  }
  
  if(!proc.start(KProcess::DontCare))
    KMessageBox::sorry(krApp,i18n("Can't open ")+"\""+term+"\"");

  chdir(save.local8Bit());
}

void KRslots::sysInfo(){
  KProcess proc;
  proc << "kcmshell" << "System/ksysctrl";
  if (!proc.start(KProcess::DontCare)){
    KMessageBox::sorry(krApp,i18n("Can't find \"KsysCtrl\". Please install KDE admin package"));
  }
}

void KRslots::multiRename(){
	QStringList lst = Krusader::supportedTools();
	int i = lst.findIndex("RENAME");
	if (i == -1){
  	KMessageBox::sorry(krApp,i18n("Can't find a batch renamer tool.\nYou can get Krename at http://www.krename.net"));
  	return;
	}
	QString pathToRename = lst[i+1];
	
  QStringList names;
  ((ListPanel*)ACTIVE_PANEL)->getSelectedNames(&names);
	KURL::List* urls = ACTIVE_FUNC->files()->vfs_getFiles(&names);

	if( urls->isEmpty() ){
		delete urls;
		return;
	}

	KShellProcess proc;
	proc << pathToRename;

	for( KURL::List::iterator u=urls->begin(); u != urls->end(); ++u){
    if( QFileInfo((*u).path()).isDir() ) proc << "-r";
		proc << "\"" + (*u).path() + "\""; // patch thanks to Tobias Vogele
	}

	proc.start(KProcess::DontCare);
	delete urls;
}

void KRslots::rootKrusader()
{
  if( !KrServices::cmdExist( "krusader" ) || !KrServices::cmdExist( "kdesu" ) )
  {
    KMessageBox::sorry( krApp, i18n( "Can't start root mode krusader, because krusader or kdesu is missing from the path. Please configure the dependencies in Konfigurator!" ) );
    return;
  }
  
  KShellProcess proc;
  proc << KrServices::fullPathName( "kdesu" ) << QString("'") + KrServices::fullPathName( "krusader" ) +
       " --left=\"" +MAIN_VIEW->left->func->files()->vfs_getOrigin().url() +
       "\" --right=\""+MAIN_VIEW->right->func->files()->vfs_getOrigin().url() + "\"'";

  proc.start(KProcess::DontCare);
}

// settings slots
void KRslots::configToolbar(){
  KEditToolbar dlg(krApp->factory());
  if (dlg.exec()) krApp->updateGUI();
}

void KRslots::configKeys(){
  KKeyDialog::configure(krApp->actionCollection(), 0, true);
}

// misc
void KRslots::changeTrashIcon(){
	// update trash bin icon - this is "stolen" konqi code
	// Copyright (C) 2000  David Faure <faure@kde.org>
	KURL trash;
	trash.setPath(KGlobalSettings::trashPath());
	KURL::List lst;
	lst.append(trash);
	KDirNotify_stub allDirNotify("*","KDirNotify*");
	allDirNotify.FilesChanged( lst );
	// end of konqi code
}

// F2
void KRslots::terminal()       { ACTIVE_FUNC->terminal();   }
// F3
void KRslots::view()	         { ACTIVE_FUNC->view();       }
// F4
void KRslots::edit()           { ACTIVE_FUNC->editFile();   }
// F5
void KRslots::copyFiles()      { ACTIVE_FUNC->copyFiles();  }
// F6
void KRslots::moveFiles()      { ACTIVE_FUNC->moveFiles();  }	
// F7
void KRslots::mkdir()          { ACTIVE_FUNC->mkdir();      }
// F8
void KRslots::deleteFiles()    { ACTIVE_FUNC->deleteFiles();}     	
// F9
void KRslots::rename()         { ACTIVE_FUNC->rename();     }

// Shift F3
void KRslots::viewDlg(){
  // ask the user for a url to view
  KChooseDir *chooser = new KChooseDir( 0,i18n("Enter a URL to view:"), QString::null);
  QString dest = chooser->dest;
  if ( dest == QString::null ) return ; // the usr canceled
  else
  {
    /* vfs::fromPathOrURL requires fullpath, so we check whether it is relative  */
    if ( !dest.contains( ":/" ) && !dest.startsWith( "/" ) )
      dest = ACTIVE_FUNC->files()->vfs_getOrigin().prettyURL(1)+dest; /* it's full path now */
    
    KrViewer::view( vfs::fromPathOrURL(dest) ); // view the file
  }
  // nothing more to it!
}

// Shift F4
void KRslots::editDlg(){
  // ask the user for the filename to edit
  KChooseDir *chooser = new KChooseDir( 0,i18n("Enter the filename to edit:"), QString::null);
  QString dest = chooser->dest;
  if ( dest == QString::null ) return ; // the usr canceled
  else
  {
    /* vfs::fromPathOrURL requires fullpath, so we check whether it is relative  */
    if ( !dest.contains( ":/" ) && !dest.startsWith( "/" ) )
      dest = ACTIVE_FUNC->files()->vfs_getOrigin().prettyURL(1) + dest; /* it's full path now */

    KrViewer::edit( vfs::fromPathOrURL( dest ), true );
  }
  // nothing more to it!
}

void KRslots::duplicateTab() {
  ACTIVE_PANEL_MANAGER->slotNewTab(ACTIVE_PANEL->virtualPath);
}

void KRslots::newTab() {
  ACTIVE_PANEL_MANAGER->slotNewTab();
}

void KRslots::nextTab() {
  ACTIVE_PANEL_MANAGER->slotNextTab();
}

void KRslots::previousTab() {
  ACTIVE_PANEL_MANAGER->slotPreviousTab();
}

void KRslots::newTab(QListViewItem *item) {
  if (!item) return;
  KrViewItem *it = dynamic_cast<KrViewItem*>(item);
  if (it->isDir()) {
    ACTIVE_PANEL_MANAGER->slotNewTab(ACTIVE_PANEL->virtualPath + "/" + it->name());
    //kdWarning() << ACTIVE_PANEL->virtualPath << it->name()<<endl;
  }
}

void KRslots::closeTab() {
  ACTIVE_PANEL_MANAGER->slotCloseTab();
}

void KRslots::slotSplit()
{
  QStringList list;
  QString name;

  ((ListPanel*)ACTIVE_PANEL)->getSelectedNames(&list);

  // first, see if we've got exactly 1 selected file, if not, try the current one
  if (list.count() == 1) name = list[0];

  if ( name.isEmpty() ) {
    // if we got here, then one of the panel can't be sure what file to diff
    KMessageBox::error(0,i18n("Don't know which file to split."));
    return;
  }

  QString fileName = ACTIVE_FUNC->files()->vfs_getFile(name).prettyURL(-1,KURL::StripFileProtocol);
  if( fileName == QString::null )
    return;

  if ( ACTIVE_FUNC->files()->vfs_search( name )->vfile_isDir() ) {
    KMessageBox::sorry( krApp, i18n( "You can't split a directory!" ) );
    return ;
  }

  QString destDir  = ACTIVE_PANEL->otherPanel->func->files()->vfs_getOrigin().prettyURL(-1,KURL::StripFileProtocol);

  SplitterGUI splitterGUI( MAIN_VIEW, fileName, destDir );

  if( splitterGUI.result() == QDialog::Accepted )
  {
    bool splitToOtherPanel = ( splitterGUI.getDestinationDir() == ACTIVE_PANEL->otherPanel->virtualPath );

    Splitter split( MAIN_VIEW, fileName, splitterGUI.getDestinationDir() );
    split.split( splitterGUI.getSplitSize() );

    if ( splitToOtherPanel )
      ACTIVE_PANEL->otherPanel->func->refresh();
  }
}

void KRslots::slotCombine()
{
  QStringList   list;
  QString       fileName;
  bool          unixStyle = false;
  bool          windowsStyle = false;
  QString       commonName = QString::null;
  unsigned int  commonLength = 0;

  ((ListPanel*)ACTIVE_PANEL)->getSelectedNames(&list);
  if ( list.isEmpty() )
  {
    KMessageBox::error(0,i18n("Don't know which files to combine."));
    return;
  }

  /* checking splitter names */
  for ( QStringList::Iterator it = list.begin(); it != list.end(); ++it )
  {
    QString name = ACTIVE_FUNC->files()->vfs_getFile(*it).prettyURL(-1,KURL::StripFileProtocol);
    if( name == QString::null )
      return;

    if ( ACTIVE_FUNC->files()->vfs_search( *it )->vfile_isDir() ) {
      KMessageBox::sorry( krApp, i18n( "You can't combine a directory!" ) );
      return ;
    }

    if( !unixStyle )
    {
      int extPos = name.findRev( '.' );

      QString ext = name.mid( extPos + 1 );
      bool isExtInt;
      ext.toInt( &isExtInt, 10 );

      if( extPos < 1 || ext.isEmpty() || ( ext != "crc" && !isExtInt ) )
      {
        if( windowsStyle )
        {
          KMessageBox::error(0,i18n("Not a splitted file %1!").arg( name ));
          return;
        }
        unixStyle = true;
      }
      else
      {
        name.truncate( extPos );

        if( ext != "crc" )
          windowsStyle = true;

        if( fileName.isEmpty() )
           fileName = name;
        else if( fileName != name )
        {
          KMessageBox::error(0,i18n("Select only one splitted file!"));
          return;
        }
      }
    }

    if( unixStyle )
    {
      bool error = true;

      do
      {
        QString shortName   = *it;
        QChar   lastChar  = shortName.at( shortName.length()-1 );

        if( lastChar.isLetter() )
        {
          char fillLetter = ( lastChar.upper() == lastChar ) ? 'A' : 'a';

          if( commonName.isNull() )
          {
            commonLength = shortName.length();
            commonName = shortName;

            while ( commonName.length() )
            {
              QString shorter  = commonName.left( commonName.length() - 1 );
              QString testFile = shorter.leftJustify( commonLength, fillLetter );

              if( ACTIVE_FUNC->files()->vfs_search( testFile ) == 0 )
                break;
              else
              {
                commonName = shorter;
                fileName = ACTIVE_FUNC->files()->vfs_getOrigin().prettyURL(1,KURL::StripFileProtocol) + testFile;
              }
            }

            error = ( commonName == shortName );
          }
          else if( commonLength == shortName.length() && shortName.startsWith( commonName ) )
            error = false;
        }
      }while ( false );

      if( error )
      {
        KMessageBox::error(0,i18n("Not a splitted file %1!").arg( name ));
        return;
      }
    }
  }

  // ask the user for the copy dest
  KChooseDir *chooser = new KChooseDir( 0, i18n( "Combining %1.* to directory:" ).arg( fileName ),
                                        ACTIVE_PANEL->otherPanel->getPath() );
  QString dest = chooser->dest;
  if ( dest == QString::null )
    return ; // the usr canceled

  bool combineToOtherPanel = ( dest == ACTIVE_PANEL->otherPanel->virtualPath );

  Combiner combine( MAIN_VIEW, fileName, dest, unixStyle );
  combine.combine();

  if ( combineToOtherPanel )
    ACTIVE_PANEL->otherPanel->func->refresh();
}

void KRslots::userMenu() {
  //UserMenu um;
  //um.exec();
  krApp->userMenu->exec();
}

void KRslots::slotSynchronizeDirs() {
  SynchronizerGUI *sync = new SynchronizerGUI( MAIN_VIEW,
                MAIN_VIEW->left->func->files()->vfs_getOrigin().prettyURL(0,KURL::StripFileProtocol),
                MAIN_VIEW->right->func->files()->vfs_getOrigin().prettyURL(0,KURL::StripFileProtocol) );

  bool refresh = sync->wasSynchronization();
  delete sync;

  if( refresh )
    REFRESH_BOTH_PANELS;
}

void KRslots::updatePopupPanel(QListViewItem *it) {
	// which panel to display on?
	ListPanel *lp = 0;
	if (ACTIVE_PANEL->popup->isHidden() &&
		 ACTIVE_PANEL->otherPanel->popup->isHidden()) return;
	if (ACTIVE_PANEL->popup->isShown())
		lp = ACTIVE_PANEL;
	else if (ACTIVE_PANEL->otherPanel->popup->isShown())
		lp = ACTIVE_PANEL->otherPanel;

	KrViewItem *item = dynamic_cast<KrViewItem*>(it);
	KURL url;
	if (item->name()!="..") // updir
		url = ACTIVE_FUNC->files()->vfs_getFile(item->name());
	lp->popup->update(url);
}

void KRslots::compareDirs()          
{
  ACTIVE_PANEL->compareDirs();
  ACTIVE_PANEL->otherPanel->compareDirs();
}

void KRslots::compareSetup()
{
  for( int i=0; Krusader::compareArray[i] != 0; i++ )
    if( (*Krusader::compareArray[i])->isChecked() )
    {
      krConfig->setGroup( "Private" );
      krConfig->writeEntry( "Compare Mode", i );
      break;
    }
}

void KRslots::profiles()
{
  ProfileManager profileManager( "Panel" );
  profileManager.hide();
  connect( &profileManager, SIGNAL( saveToProfile( QString ) ), this, SLOT( savePanelProfiles( QString ) ) );
  connect( &profileManager, SIGNAL( loadFromProfile( QString ) ), this, SLOT( loadPanelProfiles( QString ) ) );
  profileManager.profilePopup();
}

void KRslots::loadPanelProfiles( QString group )
{
  krConfig->setGroup( group );
  MAIN_VIEW->leftMng->loadSettings( krConfig, "Left Tabs" );
  krConfig->setGroup( group );
  MAIN_VIEW->leftMng->setCurrentTab( krConfig->readNumEntry( "Left Active Tab", 0 ) );
  krConfig->setGroup( group );
  MAIN_VIEW->rightMng->loadSettings( krConfig, "Right Tabs" );
  krConfig->setGroup( group );
  MAIN_VIEW->rightMng->setCurrentTab( krConfig->readNumEntry( "Right Active Tab", 0 ) );
  krConfig->setGroup( group );  
  if( krConfig->readBoolEntry( "Left Side Is Active", true ) )
    MAIN_VIEW->left->slotFocusOnMe();
  else
    MAIN_VIEW->right->slotFocusOnMe();
}

void KRslots::savePanelProfiles( QString group )
{
  krConfig->setGroup( group );
  
  MAIN_VIEW->leftMng->saveSettings( krConfig, "Left Tabs" );
  krConfig->writeEntry( "Left Active Tab", MAIN_VIEW->leftMng->activeTab() );
  MAIN_VIEW->rightMng->saveSettings( krConfig, "Right Tabs" );
  krConfig->writeEntry( "Right Active Tab", MAIN_VIEW->rightMng->activeTab() );
  krConfig->writeEntry( "Left Side Is Active", MAIN_VIEW->activePanel->isLeft() );
}

#include "krslots.moc"
