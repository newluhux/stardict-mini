#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// index 中单词最长长度
#define WORDLENMAX 256

// stardict 结构数组排序方式
#define STARDICT_FILE_IDX 0
#define STARDICT_FILE_DICT 1
#define STARDICT_FILE_IFO 2
// 一个 startdict 结构最大打开的文件数量
#define STARDICT_FILES 3

struct stardict {
	char *filename[STARDICT_FILES]; // 存放数据文件的位置和文件名
	unsigned char *file_mmap[STARDICT_FILES]; // 存放数据文件 mmap 之后的内存地址
	off_t mmap_size[STARDICT_FILES]; // 存放 mmap 之后内存区域的大小
};
typedef struct stardict stardict;

stardict *stardict_init(void);
int stardict_free(stardict *sd);
int stardict_open(stardict *sd,char *files[]);

struct stardict_index {
	char *word; // 单词的字符串
	size_t len; // 单词的长度
	uint32_t offset; // 单词对应内容在.dict文件中的位置
	uint32_t size;   // 单词对应内容的长度
};
typedef struct stardict_index stardict_index;

stardict_index *stardict_index_init(void);
int stardict_index_clean(stardict_index *index);
int stardict_index_free(stardict_index *index);
size_t stardict_index_load(unsigned char *addr,stardict_index *index);

int stardict_search(stardict *sd,char *word,stardict_index *result);
int stardict_data_print(FILE *out,stardict *sd,stardict_index *index);

// stardict 官方建议的函数
int stardict_strcmp(char *s1,char *s2) {
	int a;
	a = strcasecmp(s1,s2);
	if (a == 0)
		return (strcmp(s1,s2));
	else
		return a;
}

// 初始化一个结构存储一个词典的文件名，mmap内存映射等
stardict *stardict_init(void) {
	stardict *sd = (stardict *)calloc(1,sizeof(stardict));
	int i;
	for (i=0;i<STARDICT_FILES;i++) {
		sd->file_mmap[i] = MAP_FAILED;
		sd->mmap_size[i] = -1;
	}
	return sd;
}

// 关闭结构内的文件mmap内存映射，释放结构内的指针，释放结构本身
int stardict_free(stardict *sd) {
	if (sd == NULL)
		return -1;
	int i;
	for (i=0;i<STARDICT_FILES;i++) {
		if (sd->file_mmap[i] != MAP_FAILED &&
			sd->mmap_size[i] != -1)
			munmap(sd->file_mmap[i],sd->mmap_size[i]);
	}
	return 1;
}

// 从files数组中得到文件名并打开，信息存放在sd结构

// 示例
/*

char *files[STARDICT_FILES];
files[STARDICT_FILE_IDX] = "/path/to/xxx.idx";
files[STARDICT_FILE_DICT] = "/path/to/xxx.dict";
files[STARDICT_FILE_IFO] = "/path/to/xxx.ifo";
stardict_open(sd,files);
*/
int stardict_open(stardict *sd,char *files[]) {
	if (sd == NULL || files == NULL)
		return -1;

	int i;
	int fd;
	struct stat sb;
	for (i=0;i<STARDICT_FILES;i++) {
		fd = -1;
		if (files[i] == NULL)
			goto err;
		fd = open(files[i],O_RDONLY);
		if (fd == -1)
			goto err;
		if (fstat(fd,&sb) == -1)
			goto err;
		sd->mmap_size[i] = sb.st_size;
		sd->file_mmap[i] = (unsigned char *)mmap(NULL,sd->mmap_size[i],
							PROT_READ,MAP_SHARED,
							fd,0);
		if (sd->file_mmap[i] == MAP_FAILED)
			goto err;
		close(fd);
	}
	return 1;
err:
	if (fd != -1)
		close(fd);
	for (i=0;i<STARDICT_FILES;i++) {
		if (sd->file_mmap[i] != MAP_FAILED)
			munmap(sd->file_mmap[i],sd->mmap_size[i]);
		sd->mmap_size[i] = -1;
		sd->file_mmap[i] = MAP_FAILED;
	}
	return -1;
}











// 初始化一个结构
stardict_index *stardict_index_init(void) {
	stardict_index *index = (stardict_index *)calloc(1,sizeof(stardict_index));
	index->len = -1;
	index->offset = -1;
	index->size = -1;
	return index;
}

// 释放本身
int stardict_index_free(stardict_index *index) {
	if (index == NULL)
		return -1;
	free(index);
	return 1;
}

// 从一块从addr开始的内存中读取一段内容，加载到index结构
// 返回读取的字节数
size_t stardict_index_load(unsigned char *addr,stardict_index *index) {
	if (addr == NULL || index == NULL)
		return -1;

	unsigned char *saveptr = addr;
	char *tempstr;

	index->len = strlen((char *)addr);
	index->word = (char *)addr;
	addr += index->len + sizeof(char);
	index->offset = ntohl(*((uint32_t *)addr));
	addr += sizeof(uint32_t);
	index->size = ntohl(*((uint32_t *)addr));
	addr += sizeof(uint32_t);
	return (size_t)(addr - saveptr);
}

// 在词典sd中搜索单词word结果存放在result
int stardict_search(stardict *sd,char *word,stardict_index *result) {
	if (sd == NULL || word == NULL || result == NULL)
		return -1;
	int len = strnlen(word,WORDLENMAX);

	unsigned char *idxp = sd->file_mmap[STARDICT_FILE_IDX];
	unsigned char *idxend = (unsigned char *)idxp + sd->mmap_size[STARDICT_FILE_IDX];
	stardict_index index;

	size_t nread;
	while (idxp < idxend) {
		nread = stardict_index_load(idxp,&index);
		if (nread == -1)
			break;
		idxp += nread;
		if (stardict_strcmp(word,index.word) == 0) {
			goto found;
		}
	}

	return -1;
found:
	memcpy(result,&index,sizeof(*result));
	return 1;
}

// 把index中的offset ~ offset+size 中间的内容从数据文件中读取出来并输出到out
int stardict_data_print(FILE *out,stardict *sd,stardict_index *index) {
	if (out == NULL || sd == NULL || index == NULL)
		return -1;

	off_t offset = 0;
	off_t size = 0;

	offset += index->offset;
	size += index->size;

	void *p =  sd->file_mmap[STARDICT_FILE_DICT];
	p += offset;
	if (fwrite(p,size,1,out) != 1)
		return -1;
	fputc('\n',out);
	return 1;
}

char *strdupandstrip(char *s) {
	if (s == NULL)
		return NULL;
	char *ns = strdup(s); // 新的字符串用作操作
	char *saveptr = ns;
	int len = strlen(ns);
	int i;
	for (i=len-1;isspace(ns[i]);i--) { // 移除结尾的空白
		ns[i] = '\0';
	}
	for (;isspace(*ns);ns++) // 移动指针到行首非空白字符上
		;
	ns = strdup(ns); // 拷贝新的字符串
	free(saveptr); // 释放旧的字符串
	return ns;
}

char *getword(FILE *in) {
	char word[WORDLENMAX];
	int i;
	int c;

	while (((c=fgetc(in)) != EOF) && isspace(c)) // 跳过输入单词前的空白
		;
	if (c == EOF)
		return NULL;
	ungetc(c,in); // 把读出来的压回输入

	i = 0;
	while (((c=fgetc(in)) != EOF) && isalnum(c) && i < WORDLENMAX-1 ) {
		word[i] = c;
		i++;
	}
	word[i] = '\0';
	if (strnlen(word,WORDLENMAX) <= 0)
		return NULL;
	return strdupandstrip(word);
}

char *dict_files[STARDICT_FILES];

#define DEFAULT_IDX_FILE "/usr/share/stardict/default.idx"
#define DEFAULT_IFO_FILE "/usr/share/stardict/default.ifo"
#define DEFAULT_DICT_FILE "/usr/share/stardict/default.dict"

void set_dict_files(void) {
	// 环境变量指定的词典
	char *env_idx = getenv("stardict_idx");
	char *env_ifo = getenv("stardict_ifo");
	char *env_dict = getenv("stardict_dict");
	if (env_idx != NULL &&
		env_ifo != NULL &&
		env_dict != NULL) {
		dict_files[STARDICT_FILE_IDX] = env_idx;
		dict_files[STARDICT_FILE_DICT] = env_dict;
		dict_files[STARDICT_FILE_IFO] = env_ifo;
		return;
	}

	// 默认方案
	dict_files[STARDICT_FILE_IDX] = DEFAULT_IDX_FILE;
	dict_files[STARDICT_FILE_DICT] = DEFAULT_DICT_FILE;
	dict_files[STARDICT_FILE_IFO] = DEFAULT_IFO_FILE;
}

int main(int argc, char *argv[]) {
	// 指定要用的字典
	set_dict_files();

	// 词典初始化
	stardict *dic = stardict_init();
	if (stardict_open(dic,dict_files) == -1) {
		fprintf(stderr,"Can't open dictionary files ");
		perror("");
		exit(EXIT_FAILURE);
	}
	stardict_index index;

	int i;
	if (argc > 1) { // 翻译程序参数里面的单词
		for (i=1;i<argc;i++) {
			if (stardict_search(dic,argv[i],&index) != -1) {
				stardict_data_print(stdout,dic,&index);
			}
		}
		goto done;
	}

	char *word;
	while (1) { // 翻译标准输入的单词
		word = getword(stdin);
		if (word == NULL)
			continue;
		if (stardict_search(dic,word,&index) != -1) {
			stardict_data_print(stdout,dic,&index);
		}
		free(word);
		fflush(stdout);
	}

done:
	stardict_free(dic);
	exit(EXIT_SUCCESS);
}