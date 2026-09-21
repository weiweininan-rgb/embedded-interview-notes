/* 覆盖 CSV 表结构校验和保护帧顺序的样本索引规则。 */
#include "csv_source.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

static void write_text(const char *path, const char *text)
{
    /* 小型测试数据使解析行为不依赖主样本文件。 */
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

int main(void)
{
    const char *header = "frequency_hz,sample_index,incident_re,incident_im,reflected_re,reflected_im,transmitted_re,transmitted_im\n";
    struct csv_source source = {0};
    struct sparam_frame frame;

    assert(csv_source_open(&source, "build/does-not-exist.csv") == -1);
    write_text("build/empty.csv", "");
    assert(csv_source_open(&source, "build/empty.csv") == -1);

    /* 新帧不能从索引 1 开始，样本索引必须从 0 起。 */
    write_text("build/bad.csv",
               "frequency_hz,sample_index,incident_re,incident_im,reflected_re,reflected_im,transmitted_re,transmitted_im\n"
               "100,1,1,0,0.5,0,0.25,0\n");
    assert(csv_source_open(&source, "build/bad.csv") == 0);
    assert(csv_source_next(&source, &frame) == -1);
    assert(errno == EINVAL);
    csv_source_close(&source);

    write_text("build/good.csv", header);
    assert(csv_source_open(&source, "build/good.csv") == 0);
    assert(csv_source_next(&source, &frame) == 0);
    csv_source_close(&source);
    puts("csv tests: PASS");
    return 0;
}
