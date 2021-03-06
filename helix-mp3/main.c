#include "mp3dec.h"
#include "mp3common.h"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mp3_decoder {
    HMP3Decoder decoder;
    MP3FrameInfo frame_info;
    int frames;
    void *user_data;
    unsigned char read_buffer[MAINBUF_SIZE * 2], *read_ptr;
    //关于read_buffer的空间长度应该满足最高比特率条件下的一帧的最大大小（MAINBUF_SIZE = 1940）;从解码效率考虑，申请其两倍大小
    short int out[1152 * 2];
    int read_offset;
    int bytes_left, bytes_left_before_decoding;
};

int mp3_get_data(struct mp3_decoder *decoder) {
    FILE *fd1 = (FILE *)decoder->user_data;

    if (decoder->bytes_left > 0) {
        memmove(decoder->read_buffer, decoder->read_ptr, decoder->bytes_left);
    }
    int bytes_to_read = sizeof(decoder->read_buffer) - decoder->bytes_left;

    int read_bytes = fread(decoder->read_buffer + decoder->bytes_left, 1, bytes_to_read, fd1);
    printf("bytes_to_read: %d, read bytes: %d\n", bytes_to_read, read_bytes);
    if (read_bytes != 0) {
        decoder->read_ptr = decoder->read_buffer; 
        decoder->read_offset = 0;
        decoder->bytes_left += read_bytes;
        return 0;
    } else {
        printf("no data to read\n");
        return -1;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage:mp3_dec file.mp3 out.pcm\n");
        return 0;
    }
    struct mp3_decoder *decoder = NULL;
    FILE *fd1 = NULL, *fd2 = NULL;
    fd1 = fopen(argv[1], "rb");
    if (!fd1) {
        printf("open input file error\n");
        return 0;
    }
    fd2 = fopen(argv[2], "wb");
    if (!fd2) {
        printf("open output file error\n");
        goto error;
    }
    decoder = (struct mp3_decoder *)calloc(1, sizeof(*decoder));
    if (!decoder) {
        printf("create mp3 decoder error\n");
        goto error;
    }
    decoder->decoder = MP3InitDecoder();
    if (decoder->decoder == NULL) goto error;
    decoder->user_data = fd1;

    struct stat st;
    stat(argv[1], &st);
    long long size = st.st_size;


	char tag[10];
    int tag_len = 0;
    int read_bytes = fread(tag, 1, 10, fd1);
    if (read_bytes != 10) {
		goto error;
    } else {
        if (strncmp("ID3", tag, 3) == 0) {
            tag_len = ((tag[6] & 0x7F) << 21) |
                ((tag[7] & 0x7F) << 14) |
                ((tag[8] & 0x7F) << 7) | (tag[9] & 0x7F);
            printf("tag_len: %d\n", tag_len);
            if (tag_len >= size) {
                printf("file error\n");
				goto error;
            } else {
                fseek(fd1, tag_len - 10, SEEK_SET);
            }
        } else {
            fseek(fd1, 0, SEEK_SET);
        }
    }   


    do {
        //第一次获取数据
        if (-1 == mp3_get_data(decoder)) {
            if (decoder->bytes_left <= 0) {
                goto error;
            }
        }
        int offset = MP3FindSyncWord(decoder->read_ptr, decoder->bytes_left);
        printf("offset: %d\n", offset);
        if (offset < 0) {
            printf("can not find sys words\n");
            decoder->bytes_left = 0; 
            continue;
        }
        decoder->read_ptr += offset;
        decoder->bytes_left -= offset;
        if (decoder->bytes_left < sizeof(decoder->read_buffer)) {
            if (-1 == mp3_get_data(decoder)) {
                if (decoder->bytes_left <= 0) {
                    goto error;
                }
            }
        }
        decoder->bytes_left_before_decoding = decoder->bytes_left;
        int err = MP3Decode(decoder->decoder, &decoder->read_ptr, (int *)&decoder->bytes_left, decoder->out, 0);
        printf("ret: %d\n", err);
		if (err != ERR_MP3_NONE) {
			switch (err) {
			case ERR_MP3_INDATA_UNDERFLOW:
			    printf("ERR_MP3_INDATA_UNDERFLOW\n");
				decoder->bytes_left = 0;
				if(mp3_get_data(decoder) != 0) {
                    //没有数据可读
                    goto error;
				}
				break;
			case ERR_MP3_MAINDATA_UNDERFLOW:
				/* do nothing - next call to decode will provide more mainData */
				printf("ERR_MP3_MAINDATA_UNDERFLOW, continue to find sys words, left: %d\n", decoder->bytes_left);
				break;
			default:
				printf("unknown error: %d, left: %d\n", err, decoder->bytes_left);
				// skip this frame
				if (decoder->bytes_left > 0) {
					decoder->bytes_left --;
					decoder->read_ptr ++;
				} else {
                    printf("fatal error\n");
                    goto error;
				}
				break;
			}
		} else {
			int outputSamps;
			/* no error */
			MP3GetLastFrameInfo(decoder->decoder, &decoder->frame_info);
			/* set sample rate */
			/* write to sound device */
			outputSamps = decoder->frame_info.outputSamps;
            printf("sample rate: %d, channels: %d, outputSamps: %d\n", decoder->frame_info.samprate, decoder->frame_info.nChans, outputSamps);
			if (outputSamps > 0) {
				if (decoder->frame_info.nChans == 1) {
					int i;
					for (i = outputSamps - 1; i >= 0; i--) {
						decoder->out[i * 2] = decoder->out[i];
						decoder->out[i * 2 + 1] = decoder->out[i];
					}
					outputSamps *= 2;
				}
                fwrite(decoder->out, 1, outputSamps * sizeof(short), fd2);
			} else {
                printf("no samples\n");
			}
        }
    } while (1);
error:
    if (fd1) fclose(fd1);
    if (fd2) fclose(fd2);
    if (decoder) {
        if (decoder->decoder) MP3FreeDecoder(decoder->decoder);
        free(decoder);
    }
    return 0;
}
