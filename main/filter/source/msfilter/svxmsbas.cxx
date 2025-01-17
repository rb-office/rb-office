/**************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 *************************************************************/



// MARKER(update_precomp.py): autogen include statement, do not remove
#include "precompiled_filter.hxx"

#include <tools/debug.hxx>
#include <sfx2/objsh.hxx>
#include <sfx2/app.hxx>
#include <basic/basmgr.hxx>
#include <basic/sbmod.hxx>
#include <svx/svxerr.hxx>
#include <filter/msfilter/svxmsbas.hxx>
#include <msvbasic.hxx>
#include <filter/msfilter/msocximex.hxx>
#include <sot/storinfo.hxx>
#include <comphelper/processfactory.hxx>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/awt/XControlModel.hpp>
using namespace com::sun::star::beans;
using namespace com::sun::star::io;
using namespace com::sun::star::awt;
#include <comphelper/storagehelper.hxx>

#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/script/XLibraryContainer.hpp>
#include <com/sun/star/script/ModuleInfo.hpp>
#include <com/sun/star/script/ModuleType.hpp>
#include <com/sun/star/script/vba/XVBACompatibility.hpp>
#include <com/sun/star/script/vba/XVBAModuleInfo.hpp>

using namespace com::sun::star::container;
using namespace com::sun::star::script;
using namespace com::sun::star::uno;
using namespace com::sun::star::lang;
using namespace com::sun::star;

using rtl::OUString;

static ::rtl::OUString sVBAOption( RTL_CONSTASCII_USTRINGPARAM( "Option VBASupport 1\n" ) );

int SvxImportMSVBasic::Import( const String& rStorageName,
							   const String &rSubStorageName,
							   sal_Bool bAsComment, sal_Bool bStripped )
{
	std::vector< String > codeNames;
	return Import( rStorageName, rSubStorageName, codeNames, bAsComment, bStripped );
}

int SvxImportMSVBasic::Import( const String& rStorageName,
								const String &rSubStorageName,
								const std::vector< String >& codeNames,
								sal_Bool bAsComment, sal_Bool bStripped )
{
	int nRet = 0;
	if( bImport && ImportCode_Impl( rStorageName, rSubStorageName, codeNames,
									bAsComment, bStripped ))
		nRet |= 1;

	if (bImport)
		ImportForms_Impl(rStorageName, rSubStorageName);

	if( bCopy && CopyStorage_Impl( rStorageName, rSubStorageName ))
		nRet |= 2;

	return nRet;
}

bool SvxImportMSVBasic::ImportForms_Impl(const String& rStorageName,
	const String& rSubStorageName)
{
	SvStorageRef xVBAStg(xRoot->OpenSotStorage(rStorageName,
		STREAM_READWRITE | STREAM_NOCREATE | STREAM_SHARE_DENYALL));
	if (!xVBAStg.Is() || xVBAStg->GetError())
		return false;

	std::vector<String> aUserForms;
	SvStorageInfoList aContents;
	xVBAStg->FillInfoList(&aContents);
	for (sal_uInt16 nI = 0; nI < aContents.Count(); ++nI)
	{
		SvStorageInfo& rInfo = aContents.GetObject(nI);
		if (!rInfo.IsStream() && rInfo.GetName() != rSubStorageName)
			aUserForms.push_back(rInfo.GetName());
	}

	if (aUserForms.empty())
		return false;

	bool bRet = true;
	try
	{
		Reference<XMultiServiceFactory> xSF(comphelper::getProcessServiceFactory());

		Reference<XComponentContext> xContext;
		Reference<XPropertySet> xProps(xSF, UNO_QUERY);
		xProps->getPropertyValue(OUString(RTL_CONSTASCII_USTRINGPARAM("DefaultContext")) ) >>= xContext;


		Reference<XLibraryContainer> xLibContainer = rDocSh.GetDialogContainer();
		DBG_ASSERT( xLibContainer.is(), "No BasicContainer!" );

		String aLibName( RTL_CONSTASCII_USTRINGPARAM( "Standard" ) );
		Reference<XNameContainer> xLib;
		if (xLibContainer.is())
		{
			if( !xLibContainer->hasByName(aLibName))
				xLibContainer->createLibrary(aLibName);

			Any aLibAny = xLibContainer->getByName( aLibName );
			aLibAny >>= xLib;
		}

		if(xLib.is())
		{
			typedef std::vector<String>::iterator myIter;
			myIter aEnd = aUserForms.end();
			for (myIter aIter = aUserForms.begin(); aIter != aEnd; ++aIter)
			{
				SvStorageRef xForm (xVBAStg->OpenSotStorage(*aIter,
					STREAM_READWRITE | STREAM_NOCREATE | STREAM_SHARE_DENYALL));

				if (!xForm.Is() || xForm->GetError())
					continue;

				SvStorageStreamRef xFrame = xForm->OpenSotStream(
					String( RTL_CONSTASCII_USTRINGPARAM( "\3VBFrame" ) ),
					STREAM_STD_READ | STREAM_NOCREATE);

				if (!xFrame.Is() || xFrame->GetError())
					continue;

				SvStorageStreamRef xTypes = xForm->OpenSotStream(
					String( 'f' ), STREAM_STD_READ | STREAM_NOCREATE);

				if (!xTypes.Is() || xTypes->GetError())
					continue;

				//<UserForm Name=""><VBFrame></VBFrame>"
				String sData;
				String sLine;
				while(xFrame->ReadByteStringLine(sLine, RTL_TEXTENCODING_MS_1252))
				{
					sData += sLine;
					sData += '\n';
				}
				sData.ConvertLineEnd();

				Reference<container::XNameContainer> xDialog(
					xSF->createInstance(
						OUString(RTL_CONSTASCII_USTRINGPARAM(
							"com.sun.star.awt.UnoControlDialogModel"))), uno::UNO_QUERY);

				OCX_UserForm aForm(xVBAStg, *aIter, *aIter, xDialog, xSF );
				aForm.pDocSh = &rDocSh;
				sal_Bool bOk = aForm.Read(xTypes);
				DBG_ASSERT(bOk, "Had unexpected content, not risking this module");
				if (bOk)
					aForm.Import(xLib);
			}
		}
	}
	catch(...)
	{
		DBG_ERRORFILE( "SvxImportMSVBasic::ImportForms_Impl - any exception caught" );
		//bRet = false;
	}
	return bRet;
}


sal_Bool SvxImportMSVBasic::CopyStorage_Impl( const String& rStorageName,
									 	const String& rSubStorageName)
{
	sal_Bool bValidStg = sal_False;
	{
		SvStorageRef xVBAStg( xRoot->OpenSotStorage( rStorageName,
									STREAM_READWRITE | STREAM_NOCREATE |
									STREAM_SHARE_DENYALL ));
		if( xVBAStg.Is() && !xVBAStg->GetError() )
		{
			SvStorageRef xVBASubStg( xVBAStg->OpenSotStorage( rSubStorageName,
								 	STREAM_READWRITE | STREAM_NOCREATE |
									STREAM_SHARE_DENYALL ));
			if( xVBASubStg.Is() && !xVBASubStg->GetError() )
			{
				// then we will copy these storages into the (temporary) storage of the document
				bValidStg = sal_True;
			}
		}
	}

	if( bValidStg )
	{
		String aDstStgName( GetMSBasicStorageName() );
		SotStorageRef xDst = SotStorage::OpenOLEStorage( rDocSh.GetStorage(), aDstStgName, STREAM_READWRITE | STREAM_TRUNC );
		SotStorageRef xSrc = xRoot->OpenSotStorage( rStorageName, STREAM_STD_READ );

		// TODO/LATER: should we commit the storage?
		xSrc->CopyTo( xDst );
		xDst->Commit();
		ErrCode nError = xDst->GetError();
		if ( nError == ERRCODE_NONE )
			nError = xSrc->GetError();
		if ( nError != ERRCODE_NONE )
			xRoot->SetError( nError );
		else
			bValidStg = sal_True;
	}

	return bValidStg;
}

sal_Bool SvxImportMSVBasic::ImportCode_Impl( const String& rStorageName,
										const String &rSubStorageName,
										const std::vector< String >& codeNames,
										sal_Bool bAsComment, sal_Bool bStripped )
{
	sal_Bool bRet = sal_False;
	VBA_Impl aVBA( *xRoot, bAsComment );
	if( aVBA.Open(rStorageName,rSubStorageName) )
	{
		Reference<XLibraryContainer> xLibContainer = rDocSh.GetBasicContainer();
		DBG_ASSERT( xLibContainer.is(), "No BasicContainer!" );

		/*  Set library container to VBA compatibility mode. This will create
		    the VBA Globals object and store it in the Basic manager of the
		    document. */
		if( !bAsComment ) try
		{
			Reference< vba::XVBACompatibility >( xLibContainer, UNO_QUERY_THROW )->setVBACompatibilityMode( sal_True );
		}
		catch( Exception& )
		{
		}

		sal_uInt16 nStreamCount = aVBA.GetNoStreams();
		Reference<XNameContainer> xLib;
		String aLibName( RTL_CONSTASCII_USTRINGPARAM( "Standard" ) );
		if( xLibContainer.is() && nStreamCount )
		{
			if( !xLibContainer->hasByName( aLibName ) )
				xLibContainer->createLibrary( aLibName );

			Any aLibAny = xLibContainer->getByName( aLibName );
			aLibAny >>= xLib;
		}
		if( xLib.is() )
		{
			Reference< script::vba::XVBAModuleInfo > xVBAModuleInfo( xLib, UNO_QUERY );
			Reference< container::XNameAccess > xVBACodeNamedObjectAccess;
			if ( !bAsComment )
			{
				Reference< XMultiServiceFactory> xSF(rDocSh.GetModel(), UNO_QUERY);
				if ( xSF.is() )
				{
					try
					{
						xVBACodeNamedObjectAccess.set( xSF->createInstance( rtl::OUString(RTL_CONSTASCII_USTRINGPARAM( "ooo.vba.VBAObjectModuleObjectProvider"))), UNO_QUERY );
					}
					catch( Exception& ) { }
				}
			}
			typedef  std::hash_map< rtl::OUString, uno::Any, ::rtl::OUStringHash,
::std::equal_to< ::rtl::OUString > > NameModuleDataHash;
			typedef  std::hash_map< rtl::OUString, script::ModuleInfo, ::rtl::OUStringHash,
::std::equal_to< ::rtl::OUString > > NameModuleInfoHash;

			NameModuleDataHash moduleData;
			NameModuleInfoHash moduleInfos;

			for( sal_uInt16 i=0; i<nStreamCount;i++)
			{
				StringArray aDecompressed = aVBA.Decompress(i);
#if 0
/*  DR 2005-08-11 #124850# Do not filter special characters from module name.
    Just put the original module name and let the Basic interpreter deal with
    it. Needed for roundtrip...
 */
				ByteString sByteBasic(aVBA.GetStreamName(i),
					RTL_TEXTENCODING_ASCII_US,
						(RTL_UNICODETOTEXT_FLAGS_UNDEFINED_UNDERLINE|
						RTL_UNICODETOTEXT_FLAGS_INVALID_UNDERLINE |
						RTL_UNICODETOTEXT_FLAGS_PRIVATE_MAPTO0 |
						RTL_UNICODETOTEXT_FLAGS_NOCOMPOSITE)
				);

				const String sBasicModule(sByteBasic,
					RTL_TEXTENCODING_ASCII_US);
#else
				const String &sBasicModule = aVBA.GetStreamName( i);
#endif
				/* #117718# expose information regarding type of Module
				* Class, Form or plain 'ould VBA module with a REM statement
				* at the top of the module. Mapping of Module Name
				* to type is performed in VBA_Impl::Open() method,
				* ( msvbasic.cxx ) by examining the PROJECT stream.
				*/

				// using name from aVBA.GetStreamName
				// because the encoding of the same returned
				// is the same as the encoding for the names
				// that are keys in the map used by GetModuleType method
				const String &sOrigVBAModName = aVBA.GetStreamName( i );
				ModType mType = aVBA.GetModuleType( sOrigVBAModName );

				rtl::OUString sClassRem( RTL_CONSTASCII_USTRINGPARAM( "Rem Attribute VBA_ModuleType=" ) );

				rtl::OUString modeTypeComment;

				switch( mType )
				{
					case ModuleType::CLASS:
						modeTypeComment = sClassRem +
							::rtl::OUString( RTL_CONSTASCII_USTRINGPARAM( "VBAClassModule\n" ) );
						break;
					case ModuleType::FORM:
						modeTypeComment = sClassRem +
							::rtl::OUString( RTL_CONSTASCII_USTRINGPARAM( "VBAFormModule\n" ) );
						break;
					case ModuleType::DOCUMENT:
						modeTypeComment = sClassRem +
							::rtl::OUString( RTL_CONSTASCII_USTRINGPARAM( "VBADocumentModule\n" ) );
						break;
					case ModuleType::NORMAL:
						modeTypeComment = sClassRem +
							::rtl::OUString( RTL_CONSTASCII_USTRINGPARAM( "VBAModule\n" ) );
						break;
					case ModuleType::UNKNOWN:
						modeTypeComment = sClassRem +
							::rtl::OUString( RTL_CONSTASCII_USTRINGPARAM( "VBAUnknown\n" ) );
						break;
					default:
						DBG_ERRORFILE( "SvxImportMSVBasic::ImportCode_Impl - unknown module type" );
						break;
				}
				static ::rtl::OUString sClassOption( RTL_CONSTASCII_USTRINGPARAM( "Option ClassModule\n" ) );
				if ( !bAsComment )
				{
					modeTypeComment += sVBAOption;
					if ( mType == ModuleType::CLASS )
						modeTypeComment += sClassOption;
				}

				String sModule(sBasicModule); // #i52606# no need to split Macros in 64KB blocks any more!
				String sTemp;
				if (bAsComment)
				{
					sTemp+=String(RTL_CONSTASCII_USTRINGPARAM( "Sub " ));
					String sMunge(sModule);
					// Streams can have spaces in them, but modulenames
					// cannot !
					sMunge.SearchAndReplaceAll(' ','_');

					sTemp += sMunge;
					sTemp.AppendAscii("\n");
				};
				::rtl::OUString aSource(sTemp);

				for(sal_uLong j=0;j<aDecompressed.GetSize();j++)
				{
					if (bStripped)
					{
						String *pStr = aDecompressed.Get(j);
						bool bMac = true;
						xub_StrLen nBegin = pStr->Search('\x0D');
						if ((STRING_NOTFOUND != nBegin) && (pStr->Len() > 1) && (pStr->GetChar(nBegin+1) == '\x0A'))
							bMac = false;

						const char cLineEnd = bMac ? '\x0D' : '\x0A';
						const String sAttribute(String::CreateFromAscii(
							bAsComment ? "Rem Attribute" : "Attribute"));
						nBegin = 0;
						while (STRING_NOTFOUND != (nBegin =	pStr->Search(sAttribute, nBegin)))
						{
							if ((nBegin) && pStr->GetChar(nBegin-1) != cLineEnd)
							{
								// npower #i63766# Need to skip instances of Attribute
								// that are NOT Attribute statements
								nBegin = nBegin + sAttribute.Len();
								continue;
							}
							xub_StrLen nEnd = pStr->Search(cLineEnd ,nBegin);
							// DR #i26521# catch STRING_NOTFOUND, will loop endless otherwise
							if( nEnd == STRING_NOTFOUND )
								pStr->Erase();
							else
								pStr->Erase(nBegin, (nEnd-nBegin)+1);
						}
					}
					if( aDecompressed.Get(j)->Len() )
					{
						aSource+=::rtl::OUString( *aDecompressed.Get(j) );
					}

				}
				if (bAsComment)
				{
						aSource += rtl::OUString::createFromAscii("\nEnd Sub");
				}
				::rtl::OUString aModName( sModule );
				aSource = modeTypeComment + aSource;

				Any aSourceAny;
				OSL_TRACE("erm %d", mType );
					aSourceAny <<= aSource;
				if ( !bAsComment )
				{
					OSL_TRACE("vba processing %d", mType );
					script::ModuleInfo sModuleInfo;
					sModuleInfo.ModuleType = mType;
					moduleInfos[ aModName ] = sModuleInfo;
				}
 				moduleData[ aModName ] = aSourceAny;
			}
			// Hack for missing codenames ( only known to happen in Excel but... )
			// only makes sense to do this if we are importing non-commented basic
			if ( !bAsComment )
			{
				for ( std::vector< String >::const_iterator it = codeNames.begin(); it != codeNames.end(); ++it )
				{
					script::ModuleInfo sModuleInfo;
					sModuleInfo.ModuleType = ModuleType::DOCUMENT;
					moduleInfos[ *it ] = sModuleInfo;
					moduleData[ *it ] = uno::makeAny( sVBAOption );
				}
			}
			NameModuleDataHash::iterator it_end = moduleData.end();
			for ( NameModuleDataHash::iterator it = moduleData.begin(); it != it_end; ++it )
			{
				NameModuleInfoHash::iterator it_info = moduleInfos.find( it->first );
				if ( it_info != moduleInfos.end() )
				{
					ModuleInfo& sModuleInfo = it_info->second;
					if ( sModuleInfo.ModuleType == ModuleType::FORM )
						// hack, the module ( imo document basic should...
						// know the XModel... ) but it doesn't
						sModuleInfo.ModuleObject.set( rDocSh.GetModel(), UNO_QUERY );
					// document modules, we should be able to access
					// the api objects at this time
					else if ( sModuleInfo.ModuleType == ModuleType::DOCUMENT )
					{
						if ( xVBACodeNamedObjectAccess.is() )
						{
							try
							{
								sModuleInfo.ModuleObject.set( xVBACodeNamedObjectAccess->getByName( it->first ), uno::UNO_QUERY );
								OSL_TRACE("** Straight up creation of Module");
							}
							catch(uno::Exception& e)
							{
								OSL_TRACE("Failed to get documument object for %s", rtl::OUStringToOString( it->first, RTL_TEXTENCODING_UTF8 ).getStr() );
							}
						}
					}
					xVBAModuleInfo->insertModuleInfo( it->first, sModuleInfo );
				}

				if( xLib->hasByName( it->first ) )
					xLib->replaceByName( it->first, it->second );
				else
					xLib->insertByName( it->first, it->second );
			}
			bRet = true;
		}
	}
	return bRet;
}

/* vim: set noet sw=4 ts=4: */
