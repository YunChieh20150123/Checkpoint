/*  This file is part of Checkpoint
>	Copyright (C) 2017/2018 Bernardo Giordano
>
>   This program is free software: you can redistribute it and/or modify
>   it under the terms of the GNU General Public License as published by
>   the Free Software Foundation, either version 3 of the License, or
>   (at your option) any later version.
>
>   This program is distributed in the hope that it will be useful,
>   but WITHOUT ANY WARRANTY; without even the implied warranty of
>   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
>   GNU General Public License for more details.
>
>   You should have received a copy of the GNU General Public License
>   along with this program.  If not, see <http://www.gnu.org/licenses/>.
>   See LICENSE for information.
*/

#include "io.h"

bool fileExist(FS_Archive archive, std::u16string path)
{
	FSStream stream(archive, path, FS_OPEN_READ);
	bool exist = stream.getLoaded();
	stream.close();
	return exist;
}

void copyFile(FS_Archive srcArch, FS_Archive dstArch, std::u16string srcPath, std::u16string dstPath)
{
	u32 size = 0;
	FSStream input(srcArch, srcPath, FS_OPEN_READ);
	if (input.getLoaded())
	{
		size = input.getSize() > BUFFER_SIZE ? BUFFER_SIZE : input.getSize();
	}
	else
	{
		return;
	}
	
	FSStream output(dstArch, dstPath, FS_OPEN_WRITE, input.getSize());
	if (output.getLoaded())
	{
		u8* buf = new u8[size];
		do {
			u32 rd = input.read(buf, size);
			output.write(buf, rd);
			size_t slashpos = srcPath.rfind(u8tou16("/"));
			GUI_drawCopy(srcPath.substr(slashpos + 1, srcPath.length() - slashpos - 1), input.getOffset(), input.getSize());
		} while(!input.isEndOfFile());
		delete[] buf;		
	}

	input.close();
	output.close();
}

Result copyDirectory(FS_Archive srcArch, FS_Archive dstArch, std::u16string srcPath, std::u16string dstPath)
{
	Result res = 0;
	bool quit = false;
	Directory items(srcArch, srcPath);
	
	if (!items.getLoaded())
	{
		return items.getError();
	}
	
	for (size_t i = 0, sz = items.getCount(); i < sz && !quit; i++)
	{
		std::u16string newsrc = srcPath + items.getItem(i);
		std::u16string newdst = dstPath + items.getItem(i);
		
		if (items.isFolder(i))
		{
			res = createDirectory(dstArch, newdst);
			if (R_SUCCEEDED(res) || (u32)res == 0xC82044B9)
			{
				newsrc += u8tou16("/");
				newdst += u8tou16("/");
				res = copyDirectory(srcArch, dstArch, newsrc, newdst);
			}
			else
			{
				quit = true;
			}
		}
		else
		{
			copyFile(srcArch, dstArch, newsrc, newdst);
		}
	}
	
	return res;
}

Result createDirectory(FS_Archive archive, std::u16string path)
{
	return FSUSER_CreateDirectory(archive, fsMakePath(PATH_UTF16, path.data()), 0);
}

bool directoryExist(FS_Archive archive, std::u16string path)
{
	Handle handle;

	if (R_FAILED(FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_UTF16, path.data()))))
	{
		return false;
	}

	if (R_FAILED(FSDIR_Close(handle)))
	{
		return false;
	}

	return true;
}

void backup(size_t index)
{
	// check if multiple selection is enabled and don't ask for confirmation if that's the case
	if (!GUI_multipleSelectionEnabled())
	{
		if (!GUI_askForConfirmation("Backup selected save?"))
		{
			return;
		}
	}

	const Mode_t mode = getMode();
	const size_t cellIndex = GUI_getScrollableIndex();
	const bool isNewFolder = cellIndex == 0;
	Result res = 0;
	
	Title title;
	getTitle(title, index);
	
	if (title.getCardType() == CARD_CTR)
	{
		FS_Archive archive;
		if (mode == MODE_SAVE)
		{
			res = getArchiveSave(&archive, title.getMediaType(), title.getLowId(), title.getHighId());
		}
		else if (mode == MODE_EXTDATA)
		{
			res = getArchiveExtdata(&archive, title.getExtdataId());
		}
		
		if (R_SUCCEEDED(res))
		{
			std::string suggestion = getPathDateTime();
			
			std::u16string customPath;
			if (GUI_multipleSelectionEnabled())
			{
				customPath = isNewFolder ? u8tou16(suggestion.c_str()) : u8tou16(getPathFromCell(cellIndex).c_str());
			}
			else
			{
				customPath = isNewFolder ? getPath(suggestion) : u8tou16(getPathFromCell(cellIndex).c_str());
			}
			
			if (!customPath.compare(u8tou16(" ")))
			{
				FSUSER_CloseArchive(archive);
				return;
			}
			
			std::u16string dstPath = mode == MODE_SAVE ? title.getBackupPath() : title.getExtdataPath();
			dstPath += u8tou16("/") + customPath;
			
			if (!isNewFolder || directoryExist(getArchiveSDMC(), dstPath))
			{
				res = FSUSER_DeleteDirectoryRecursively(getArchiveSDMC(), fsMakePath(PATH_UTF16, dstPath.data()));
				if (R_FAILED(res))
				{
					FSUSER_CloseArchive(archive);
					createError(res, "Failed to delete the existing backup directory recursively.");
					return;
				}
			}
			
			res = createDirectory(getArchiveSDMC(), dstPath);
			if (R_FAILED(res))
			{
				FSUSER_CloseArchive(archive);
				createError(res, "Failed to create destination directory.");
				return;
			}
			
			std::u16string copyPath = dstPath + u8tou16("/");
			
			res = copyDirectory(archive, getArchiveSDMC(), u8tou16("/"), copyPath);
			if (R_FAILED(res))
			{
				std::string message = mode == MODE_SAVE ? "Failed to backup save." : "Failed to backup extdata.";
				FSUSER_CloseArchive(archive);
				createError(res, message);
				
				FSUSER_DeleteDirectoryRecursively(getArchiveSDMC(), fsMakePath(PATH_UTF16, dstPath.data()));
				return;
			}
			
			refreshDirectories(title.getId());
		}
		else
		{
			createError(res, "Failed to open save archive.");
		}
		
		FSUSER_CloseArchive(archive);		
	}
	else
	{
		CardType cardType = title.getSPICardType();
		u32 saveSize = SPIGetCapacity(cardType);
		u32 sectorSize = (saveSize < 0x10000) ? saveSize : 0x10000;
		
		std::string suggestion = getPathDateTime();
		
		std::u16string customPath;
		if (GUI_multipleSelectionEnabled())
		{
			customPath = isNewFolder ? u8tou16(suggestion.c_str()) : u8tou16(getPathFromCell(cellIndex).c_str());
		}
		else
		{
			customPath = isNewFolder ? getPath(suggestion) : u8tou16(getPathFromCell(cellIndex).c_str());
		}
			
		if (!customPath.compare(u8tou16(" ")))
		{
			return;
		}
		
		std::u16string dstPath = mode == MODE_SAVE ? title.getBackupPath() : title.getExtdataPath();
		dstPath += u8tou16("/") + customPath;
		
		if (!isNewFolder || directoryExist(getArchiveSDMC(), dstPath))
		{
			res = FSUSER_DeleteDirectoryRecursively(getArchiveSDMC(), fsMakePath(PATH_UTF16, dstPath.data()));
			if (R_FAILED(res))
			{
				createError(res, "Failed to delete the existing backup directory recursively.");
				return;
			}
		}
		
		res = createDirectory(getArchiveSDMC(), dstPath);
		if (R_FAILED(res))
		{
			createError(res, "Failed to create destination directory.");
			return;
		}
		
		std::u16string copyPath = dstPath + u8tou16("/") + u8tou16(title.getShortDescription().c_str()) + u8tou16(".sav");
		
		u8* saveFile = new u8[saveSize];
		for (u32 i = 0; i < saveSize/sectorSize; ++i)
		{
			res = SPIReadSaveData(cardType, sectorSize*i, saveFile + sectorSize*i, sectorSize);
			if (R_FAILED(res))
			{
				break;
			}
			GUI_drawCopy(u8tou16(title.getShortDescription().c_str()) + u8tou16(".sav"), sectorSize*(i+1), saveSize);
		}
		
		if (R_FAILED(res))
		{
			delete[] saveFile;
			createError(res, "Failed to backup save.");
			FSUSER_DeleteDirectoryRecursively(getArchiveSDMC(), fsMakePath(PATH_UTF16, dstPath.data()));
			return;
		}
		
		FSStream stream(getArchiveSDMC(), copyPath, FS_OPEN_WRITE, saveSize);
		if (stream.getLoaded())
		{
			stream.write(saveFile, saveSize);
		}
		else
		{
			delete[] saveFile;
			stream.close();
			createError(res, "Failed to backup save.");
			FSUSER_DeleteDirectoryRecursively(getArchiveSDMC(), fsMakePath(PATH_UTF16, dstPath.data()));
			return;			
		}
		
		delete[] saveFile;
		stream.close();
		refreshDirectories(title.getId());
	}
	
	createInfo("Success!", "Progress correctly saved to disk.");
}

void restore(size_t index)
{
	const Mode_t mode = getMode();
	const size_t cellIndex = GUI_getScrollableIndex();
	if (cellIndex == 0 || !GUI_askForConfirmation("Restore selected save?"))
	{
		return;
	}
	
	Result res = 0;
	
	Title title;
	getTitle(title, index);
	
	if (title.getCardType() == CARD_CTR)
	{
		FS_Archive archive;
		if (mode == MODE_SAVE)
		{
			res = getArchiveSave(&archive, title.getMediaType(), title.getLowId(), title.getHighId());
		}
		else if (mode == MODE_EXTDATA)
		{
			res = getArchiveExtdata(&archive, title.getExtdataId());
		}
		
		if (R_SUCCEEDED(res))
		{
			std::u16string srcPath = mode == MODE_SAVE ? title.getBackupPath() : title.getExtdataPath();
			srcPath += u8tou16("/") + u8tou16(getPathFromCell(cellIndex).c_str()) + u8tou16("/");
			std::u16string dstPath = u8tou16("/");
			
			if (mode != MODE_EXTDATA)
			{
				res = FSUSER_DeleteDirectoryRecursively(archive, fsMakePath(PATH_UTF16, dstPath.data()));
			}
			
			res = copyDirectory(getArchiveSDMC(), archive, srcPath, dstPath);
			if (R_FAILED(res))
			{
				std::string message = mode == MODE_SAVE ? "Failed to restore save." : "Failed to restore extdata.";
				FSUSER_CloseArchive(archive);
				createError(res, message);
				return;
			}
			
			if (mode == MODE_SAVE)
			{
				res = FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
				if (R_FAILED(res))
				{
					FSUSER_CloseArchive(archive);
					createError(res, "Failed to commit save data.");
					return;			
				}
				
				u8 out;
				u64 secureValue = ((u64)SECUREVALUE_SLOT_SD << 32) | (title.getUniqueId() << 8);
				res = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &secureValue, 8, &out, 1);
				if (R_FAILED(res))
				{
					FSUSER_CloseArchive(archive);
					createError(res, "Failed to fix secure value.");
					return;			
				}
			}
		}
		else
		{
			createError(res, "Failed to open save archive.");
		}
		
		FSUSER_CloseArchive(archive);		
	}
	else
	{
		CardType cardType = title.getSPICardType();
		u32 saveSize = SPIGetCapacity(cardType);
		u32 pageSize = SPIGetPageSize(cardType);

		std::u16string srcPath = mode == MODE_SAVE ? title.getBackupPath() : title.getExtdataPath();
		srcPath += u8tou16("/") + u8tou16(getPathFromCell(cellIndex).c_str()) + u8tou16("/") + u8tou16(title.getShortDescription().c_str()) + u8tou16(".sav");

		u8* saveFile = new u8[saveSize];
		FSStream stream(getArchiveSDMC(), srcPath, FS_OPEN_READ);
		
		if (stream.getLoaded())
		{
			stream.read(saveFile, saveSize);
		}
		res = stream.getResult();
		stream.close();
		
		if (R_FAILED(res))
		{
			delete[] saveFile;
			createError(res, "Failed to read save file backup.");
			return;
		}

		for (u32 i = 0; i < saveSize/pageSize; ++i)
		{
			res = SPIWriteSaveData(cardType, pageSize*i, saveFile + pageSize*i, pageSize);
			if (R_FAILED(res))
			{
				break;
			}
			GUI_drawCopy(u8tou16(title.getShortDescription().c_str()) + u8tou16(".sav"), pageSize*(i+1), saveSize);
		}

		if (R_FAILED(res))
		{
			delete[] saveFile;
			createError(res, "Failed to restore save.");
			return;
		}		

		delete[] saveFile;
	}
	
	createInfo("Success!", getPathFromCell(cellIndex) + " has been restored successfully.");
}

void deleteBackupFolder(std::u16string path)
{
	Result res = FSUSER_DeleteDirectoryRecursively(getArchiveSDMC(), fsMakePath(PATH_UTF16, path.data()));
	if (R_FAILED(res))
	{
		createError(res, "Failed to delete backup folder.");
	}
}