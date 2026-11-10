"""Execute real MT6363 worker/stop helpers with serial ownership mocks."""
from pathlib import Path
import argparse,shlex,subprocess,tempfile
H=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--cc',default='gcc');p.add_argument('--generate-only',action='store_true');p.add_argument('--build-dir',type=Path);a=p.parse_args()
s=(H.parent/'linux-6.1/drivers/regulator/mt6363-regulator.c').read_text()
w=s[s.index('static void mt6363_oc_irq_enable_work('):s.index('static irqreturn_t mt6363_oc_irq(')]
t=s[s.index('static void mt6363_stop_oc('):s.index('static int mt6363_regulator_probe(')]
shim=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define MT6363_MAX_REGULATOR 4
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
struct mutex {bool held;};
struct work_struct {int x;};
struct delayed_work {struct work_struct work; bool pending;};
#define to_delayed_work(p) container_of(p,struct delayed_work,work)
struct mt6363_regulator_info {struct delayed_work oc_work;struct mutex oc_lock;bool stopping,irq_disabled,irq_requested;int irq;void*rdev;};
struct mt6363_regulator_data {struct mt6363_regulator_info*regulators;};
static struct mt6363_regulator_info infos[4];
static unsigned enabled,freed,cancelled;
static void mutex_lock(struct mutex*m){assert(!m->held);m->held=true;}
static void mutex_unlock(struct mutex*m){assert(m->held);m->held=false;}
static void enable_irq(int irq){assert(irq>=20&&irq<24);unsigned i=irq-20;assert(infos[i].oc_lock.held&&!infos[i].stopping);enabled++;}
static void free_irq(int irq,void*rdev){unsigned i=irq-20;assert(i<4&&rdev==infos[i].rdev);for(unsigned j=0;j<4;j++)assert(infos[j].stopping&&!infos[j].oc_lock.held);assert(infos[i].irq_requested);freed++;}
static void cancel_delayed_work_sync(struct delayed_work*w){assert(freed==2);for(unsigned j=0;j<4;j++)assert(!infos[j].irq_requested&&!infos[j].oc_lock.held);w->pending=false;cancelled++;}
'''
test=r'''
int main(void){
 struct mt6363_regulator_data data={infos};
 for(unsigned i=0;i<4;i++){infos[i].irq=20+i;infos[i].rdev=&infos[i];infos[i].oc_work.pending=true;}
 infos[0].irq_disabled=true;mt6363_oc_irq_enable_work(&infos[0].oc_work.work);
 assert(enabled==1&&!infos[0].irq_disabled&&!infos[0].oc_lock.held);
 mt6363_oc_irq_enable_work(&infos[0].oc_work.work);assert(enabled==1);
 infos[1].stopping=true;infos[1].irq_disabled=true;mt6363_oc_irq_enable_work(&infos[1].oc_work.work);assert(enabled==1);
 infos[0].irq_requested=infos[2].irq_requested=true;infos[2].irq_disabled=true;
 mt6363_stop_oc(&data);assert(freed==2&&cancelled==4);
 for(unsigned i=0;i<4;i++){assert(!infos[i].oc_work.pending);mt6363_oc_irq_enable_work(&infos[i].oc_work.work);}
 assert(enabled==1);mt6363_stop_oc(&data);assert(freed==2&&cancelled==8);
 puts("PASS actual OC worker/stop: enable once, stop suppression, all stops before frees, all frees before cancellation, repeat stop; serial model only");
}
'''
text=shim+w+t+test

def run(b):
 b.mkdir(parents=True,exist_ok=True);f=b/'oc_model.c';f.write_text(text);exe=b/'oc_model'
 if not a.generate_only:
  subprocess.run(shlex.split(a.cc)+['-std=gnu11','-Wall','-Wextra','-Werror','-O0','-g',str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if a.build_dir:
 b=a.build_dir.resolve()
 if H not in b.parents:p.error('build directory must be strict tests descendant')
 run(b)
else:
 if a.generate_only:p.error('generate-only requires build-dir')
 with tempfile.TemporaryDirectory(dir=H) as d:run(Path(d))
