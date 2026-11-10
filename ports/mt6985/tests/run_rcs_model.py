#!/usr/bin/env python3
"""Execute the actual RCS handler with literal EINT/ACK ordering oracles."""
from pathlib import Path
import argparse, shlex, subprocess, tempfile
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='gcc')
p.add_argument('--generate-only',action='store_true')
p.add_argument('--build-dir',type=Path)
a=p.parse_args()
s=(HERE.parent/'linux-6.1/drivers/spmi/spmi-mt6985-experimental.c').read_text()
start=s.index('static irqreturn_t mt6985_pmif_rcs_thread(')
end=s.index('static void mt6985_pmif_rcs_exit(',start)
handler=s[start:end]
assert handler.count('static irqreturn_t')==1
shim=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define __iomem
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define READ_ONCE(x) (x)
#define MT6985_SPMI_SLV_3_0_EINT 0
struct irq_domain { unsigned mappings[16]; };
struct mt6985_pmif_experimental { void *spmimst_base; const unsigned *spmimst_regs; bool rcs_enabled[16]; struct irq_domain *rcs_domain; };
static uint32_t words[4];
static unsigned stage, sid_cursor, delivered[16], n_delivered;
static bool read_ack;
static struct irq_domain domain;
static struct mt6985_pmif_experimental *current;
static uint32_t readl(void *reg) {
 unsigned word=((unsigned char*)reg-(unsigned char*)words)/4;
 assert(!read_ack && sid_cursor<16 && word==sid_cursor/4);
 read_ack=true; return words[word];
}
static void writel(uint32_t v,void *reg) {
 unsigned word=((unsigned char*)reg-(unsigned char*)words)/4;
 assert(read_ack && word==sid_cursor/4);
 assert(v==(0xffU<<((sid_cursor%4)*8)));
 words[word]&=~v; read_ack=false; sid_cursor++; stage++;
}
static unsigned irq_find_mapping(struct irq_domain *d,unsigned sid) {
 assert(sid<16 && sid_cursor==sid+1 && !read_ack);
 return d->mappings[sid];
}
static void handle_nested_irq(unsigned irq) {
 assert(irq && n_delivered<16 && !read_ack);
 unsigned sid=irq-100;
 assert(sid<16 && sid_cursor==sid+1);
 delivered[n_delivered++]=sid;
 /* A child may update a SID filter during callback, not blocked by a parent mutex. */
 if(sid==4) current->rcs_enabled[5]=false;
}
'''
tests=r'''
int main(void) {
 const unsigned offsets[]={0x20,0x24,0x28,0x2c};
 struct mt6985_pmif_experimental pmif={0};
 /* base is deliberately byte-addressable, so exact source offsets select words. */
 unsigned char memory[64]={0};
 (void)memory;
 pmif.spmimst_base=(unsigned char*)words-0x20;
 pmif.spmimst_regs=offsets; pmif.rcs_domain=&domain; current=&pmif;
 for(unsigned i=0;i<16;i++) {pmif.rcs_enabled[i]=true; domain.mappings[i]=100+i;}
 words[0]=0x80402001; words[1]=0x08040201; words[2]=0x10204080; words[3]=0x01020408;
 pmif.rcs_enabled[3]=false; domain.mappings[7]=0;
 assert(mt6985_pmif_rcs_thread(9,&pmif)==IRQ_HANDLED);
 assert(sid_cursor==16 && stage==16 && n_delivered==13);
 unsigned expected[]={0,1,2,4,6,8,9,10,11,12,13,14,15};
 for(unsigned i=0;i<n_delivered;i++) assert(delivered[i]==expected[i]);
 for(unsigned i=0;i<4;i++) assert(words[i]==0);
 puts("ok 1 - real RCS handler: exact byte ACK before dispatch; disabled/missing-map skipped; callback may mask later SID");
 sid_cursor=stage=n_delivered=0;
 assert(mt6985_pmif_rcs_thread(9,&pmif)==IRQ_HANDLED);
 assert(sid_cursor==16 && stage==16 && n_delivered==0);
 puts("ok 2 - zero status still clears all SID bytes as vendor; no dispatch");
 puts("PASS RCS serial handler model; not IRQ lifecycle, concurrency, pending replay or hardware validation");
}
'''
# Avoid pointer-before-array UB: backing memory includes the EINT offset prefix.
shim=shim.replace('static uint32_t words[4];','static uint32_t backing[16];\n#define words (backing+8)')
tests=tests.replace('(unsigned char*)words-0x20','(unsigned char*)backing')
text=shim+handler+tests

def run(b):
 b.mkdir(parents=True,exist_ok=True)
 f=b/'rcs_model.c';exe=b/'rcs_model';f.write_text(text)
 if a.generate_only:return
 subprocess.run(shlex.split(a.cc)+['-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-O0','-g',str(f),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
if a.build_dir:
 b=a.build_dir.resolve()
 if HERE not in b.parents:p.error('build dir must be strict tests descendant')
 run(b)
else:
 if a.generate_only:p.error('generate-only requires build-dir')
 with tempfile.TemporaryDirectory(dir=HERE,prefix='.rcs-') as d:run(Path(d))
