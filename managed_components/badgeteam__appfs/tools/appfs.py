
import binascii

APPFS_SECTOR_SIZE = 64 * 1024
APPFS_META_CNT = 2
APPFS_META_DESC_SZ = 128
APPFS_PAGES = 255
APPFS_MAGIC = "AppFsDsc"

APPFS_METADATA_SIZE = APPFS_META_DESC_SZ * (APPFS_PAGES + 1)

APPFS_USE_DATA = 0
APPFS_USE_FREE = 0xff
APPFS_ILLEGAL = 0x55

class AppFSHeader:
    def __init__(self, data = None):
        if data == None:
            data = bytes([0xFF] * 128)
        if len(data) != 128:
            raise ValueError
        self.magic = data[0:8]
        self.serial = data[8:12]
        self.crc32 = data[12:16]
        self.reserved = data[16:]

    def serialize(self, noCrc = False):
        return self.magic + self.serial + (b"\x00\x00\x00\x00" if noCrc else self.crc32) + self.reserved

    def get_magic(self):
        return self.magic

    def set_magic(self, value = b"AppFsDsc"):
        if len(value) != 8:
            raise ValueError
        self.magic = bytes(value)

    def check_magic(self):
        return self.magic == b"AppFsDsc"

    def get_serial(self):
        return int.from_bytes(self.serial, "little")

    def set_serial(self, value):
        if isinstance(value, int):
            value = value.to_bytes(4, 'little')
        if not isinstance(value, bytes) or len(value) != 4:
            raise ValueError("Serial should be a bytes object consisting of 4 bytes")
        self.serial = value

    def get_crc32(self):
        return self.crc32

    def set_crc32(self, value):
        if isinstance(value, int):
            value = value.to_bytes(4, 'little')
        if not isinstance(value, bytes) or len(value) != 4:
            raise ValueError("CRC32 should be a bytes object consisting of 4 bytes")
        self.crc32 = value


class AppFSPageInfo:
    def __init__(self, data = None):
        if data == None:
            data = bytes([0xFF] * 128)
        if len(data) != 128:
            raise ValueError

        self.raw = data
        self.name = data[0:48]
        self.title = data[48:112]
        self.size = data[112:116]
        self.nextPage = data[116:117]
        self.used = data[117:118]
        self.version = data[118:120]
        self.reserved = data[120:]

    def get_used(self):
        return int.from_bytes(self.used, "little")

    def set_used(self, used):
        self.used = used.to_bytes(1, byteorder="little")

    def get_name(self):
        data = self.name
        terminator = 0
        for index in range(len(self.name)):
            if self.name[index] == 0:
                terminator = index
                break
        data = data[:terminator]
        return data.decode("ascii")

    def set_name(self, name):
        name = name.encode("ascii")
        name = name[:47]
        name += bytes([0] * (48 - len(name)))
        self.name = name

    def get_title(self):
        data = self.title
        terminator = 0
        for index in range(len(self.title)):
            if self.title[index] == 0:
                terminator = index
                break
        data = data[:terminator]
        return data.decode("ascii")

    def set_title(self, title):
        title = title.encode("ascii")
        title = title[:63]
        title += bytes([0] * (64 - len(title)))
        self.title = title

    def get_size(self):
        return int.from_bytes(self.size, "little")

    def set_size(self, size):
        self.size = size.to_bytes(4, 'little')

    def get_next_page(self):
        return int.from_bytes(self.nextPage, "little")

    def set_next_page(self, page):
        self.nextPage = page.to_bytes(1, 'little')

    def get_version(self):
        return int.from_bytes(self.version, "little")

    def set_version(self, version):
        self.version = version.to_bytes(2, 'little')

    def serialize(self):
        return self.name + self.title + self.size + self.nextPage + self.used + self.version + self.reserved

class AppFSMeta:
    def __init__(self, data = None, index = 0):
        self.index = index
        if data == None:
            data = bytes([0xFF] * (128 + 128 * APPFS_PAGES))
        if len(data) != 128 + 128 * APPFS_PAGES:
            raise ValueError("Invalid metadata length")

        self.header = AppFSHeader(data[0:128])
        self.pageInfo = []
        for page in range(0, APPFS_PAGES):
            offset = 128 + 128 * page
            self.pageInfo.append(AppFSPageInfo(data[offset:(offset + APPFS_META_DESC_SZ)]))

    def serialize(self, noCrc = False):
        data = self.header.serialize(noCrc)
        for page in range(0, APPFS_PAGES):
            pageData = self.pageInfo[page].serialize()
            data += pageData
        return data

    def calc_crc32(self):
        data = self.serialize(True)
        crc = binascii.crc32(data).to_bytes(4, byteorder='little')
        return crc

    def check_crc32(self):
        return (self.header.get_crc32() == self.calc_crc32())

    def set_size(self, partition_size):
        valid_pages = (partition_size // APPFS_SECTOR_SIZE)
        for pageIndex in range(valid_pages, APPFS_PAGES):
            self.pageInfo[pageIndex].set_used(APPFS_ILLEGAL)

    def get_size(self):
        valid_pages = 0
        for pageIndex in range(0, APPFS_PAGES):
            used = self.pageInfo[pageIndex].get_used()
            if used == APPFS_USE_DATA or used == APPFS_USE_FREE:
                valid_pages += 1
        return valid_pages * APPFS_SECTOR_SIZE

    def get_free(self):
        valid_pages = 0
        for pageIndex in range(0, APPFS_PAGES):
            used = self.pageInfo[pageIndex].get_used()
            if used == APPFS_USE_FREE:
                valid_pages += 1
        return valid_pages * APPFS_SECTOR_SIZE

    def print_usage(self):
        print("H", end = "")
        for pageIndex in range(0, APPFS_PAGES):
            t = "?"
            used = self.pageInfo[pageIndex].get_used()
            if used == APPFS_USE_DATA:
                t = "D"
            elif used == APPFS_USE_FREE:
                t = "F"
            elif used == APPFS_ILLEGAL:
                t = "X"
            print(t, end="")
            if ((pageIndex + 1) % 64 == 63):
                print("")
        print("")

    def get_next_free_page(self):
        for pageIndex in range(0, APPFS_PAGES):
            used = self.pageInfo[pageIndex].get_used()
            if used == APPFS_USE_FREE:
                return (pageIndex, self.pageInfo[pageIndex])
        return None

    def set_page(self, pageIndex, page):
        self.pageInfo[pageIndex] = page

class AppFS:
    def __init__(self, data_or_size = None):
        if isinstance(data_or_size, int):
            self.data = bytes([0xFF] * data_or_size)
            self.size = data_or_size
        elif isinstance(data_or_size, bytes):
            self.data = data_or_size
            self.size = len(data_or_size)
        else:
            raise ValueError("Expected either raw partition data as bytes or an int specifying the size of the partition")

        metadata = self.get_metadata()

        if not metadata:
            metadata = AppFSMeta()
            metadata.header.set_magic()
            metadata.set_size(self.size)
            self.set_metadata(metadata)


        metadata = self.get_metadata()
        if not metadata:
            raise ValueError("Failed to format")

    def get_sector(self, index):
        return self.data[index * APPFS_SECTOR_SIZE : (index + 1) * APPFS_SECTOR_SIZE]

    def set_sector(self, index, data):
        if len(data) != APPFS_SECTOR_SIZE:
            raise ValueError("Invalid sector size")
        b = len(self.data)
        offset = index * APPFS_SECTOR_SIZE
        self.data = self.data[:offset] + data + self.data[offset + APPFS_SECTOR_SIZE:]

    def get_metadata(self, index = None):
        if index == None:
            result = None
            for search_index in range(0, APPFS_META_CNT):
                current_meta = self.get_metadata(search_index)
                if current_meta.header.check_magic():
                    if current_meta.check_crc32():
                        if (result == None) or (current_meta.header.get_serial() > result.header.get_serial()):
                            result = current_meta
                    else:
                        print("Index {} invalid crc: {}".format(search_index, current_meta.header.get_crc32()))
            return result
        else:
            offset = index * APPFS_METADATA_SIZE
            raw_data = self.data[offset:(APPFS_METADATA_SIZE + offset)]
            return AppFSMeta(raw_data, index)

    def set_metadata(self, new_metadata):
        old_metadata = self.get_metadata()
        index = 0
        serial = 0
        if old_metadata:
            index = (old_metadata.index + 1) % APPFS_META_CNT
            serial = (old_metadata.header.get_serial() + 1) % 0xFFFFFFFF
        offset = index * APPFS_METADATA_SIZE
        new_metadata.header.set_serial(serial)
        new_metadata.header.set_crc32(new_metadata.calc_crc32())
        raw_metadata = new_metadata.serialize()
        self.data = self.data[:offset] + raw_metadata + self.data[APPFS_METADATA_SIZE + offset:]

    def create_file(self, name, title, version, data):
        metadata = self.get_metadata()
        file_size = len(data)
        free_size = metadata.get_free()
        if file_size > free_size:
            raise Exception("Not enough free space")

        position = 0
        previous_page = None
        while position < file_size:
            pageIndex, page = metadata.get_next_free_page()
            if previous_page:
                previous_page.set_next_page(pageIndex)
            else:
                page.set_name(name)
                page.set_title(title)
                page.set_version(version)
                page.set_size(len(data))
            page.set_next_page(0)
            page.set_used(APPFS_USE_DATA)
            sector_data = data[position:position + APPFS_SECTOR_SIZE]
            sector_data += bytes([0xFF] * (APPFS_SECTOR_SIZE - len(sector_data))) # Padding
            self.set_sector(pageIndex + 1, sector_data)
            previous_page = page
            metadata.set_page(pageIndex, page)
            position += APPFS_SECTOR_SIZE
        self.set_metadata(metadata)


    def extract_files(self):
        for pageIndex in range(0, APPFS_PAGES):
            page = self.get_metadata().pageInfo[pageIndex]
            if (page.get_used() == APPFS_USE_DATA):
                name = page.get_name()
                title = page.get_title()
                version = page.get_version()
                if name != "":
                    print(" - ", name, title, hex(version), "(" + str(page.get_size()) + " bytes)")
                    self.extract_file("output/", pageIndex)

    def extract_file(self, prefix, pageIndex):
        metadata = self.get_metadata()
        page = metadata.pageInfo[pageIndex]
        filename = page.get_name()
        filesize = page.get_size()
        fileContents = bytes([])
        remainingsize = filesize
        firstpage = True
        print("    ", end = "")
        while ((firstpage or pageIndex != 0) and remainingsize > 0):
            print(pageIndex, "", end = "")
            firstpage = False
            datainpagesize = remainingsize
            if datainpagesize > APPFS_SECTOR_SIZE:
                datainpagesize = APPFS_SECTOR_SIZE
            remainingsize -= datainpagesize
            fileContents += self.get_sector(pageIndex + 1)[:datainpagesize]
            pageIndex = page.get_next_page()
            if pageIndex > 0:
                try:
                    page = metadata.pageInfo[pageIndex]
                except:
                    raise Exception("Page out of range: " + str(pageIndex) + " (" + len(metadata.pageInfo) + ")")
            else:
                page = None
        print("")
        if not page == None:
            print("    Warning: more data after end of file?", pageIndex)

        with open(prefix+filename, "wb") as f:
            f.write(fileContents)

    def get_data(self):
        return self.data
