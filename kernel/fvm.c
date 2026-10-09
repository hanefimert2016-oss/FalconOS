/* FalconOS FVM/1 cooperative application processes.
 * Four independent, bounded virtual machines (not ELF and NOT ring 3).
 * Each has its own stack, globals, program counter and execution quota.
 * Untrusted programs cannot address kernel memory or invoke shell commands.
 */
#include "falcon.h"
#include "shfs.h"
#define FVM_PROCS 4
#define FVM_OPS 128
#define FVM_STACK 64
#define FVM_MEMORY 64
#define FVM_STEPS_PER_TICK 16
#define FVM_TOTAL_STEPS 50000u

typedef enum {VM_UNUSED,VM_RUNNING,VM_WAITING,VM_HALTED,VM_FAILED} vm_state_t;
typedef enum {OP_PUSH,OP_ADD,OP_SUB,OP_MUL,OP_DUP,OP_DROP,
              OP_LOAD,OP_STORE,OP_PRINT,OP_SLEEP,OP_JMP,OP_JZ,OP_HALT} opcode_t;
typedef struct { u8 op; i32 arg; } instruction_t;
typedef struct {
    vm_state_t state;
    char name[SHFS_PATH];
    instruction_t instructions[FVM_OPS];
    u16 count,pc,sp;
    i32 stack[FVM_STACK],locals[FVM_MEMORY];
    u32 executed,wakeup;
    i32 last_print;
    bool printed;
} vm_t;
static vm_t procs[FVM_PROCS];

static bool text_eq(const char *p,u32 n,const char *value){
    u32 i=0;
    for(;i<n&&value[i];i++)if(p[i]!=value[i])return false;
    return i==n && !value[i];
}
static bool parse_number(const char *src,u32 n,i32 *out){
    if(!n || n>11 || !out)return false;
    bool negative=false;
    if(*src=='-'){negative=true;src++;n--;}
    if(!n)return false;
    u64 val=0;
    for(u32 i=0;i<n;i++){
        if(src[i]<'0'||src[i]>'9')return false;
        val=val*10u+(u64)(src[i]-'0');
        if(val>(negative?2147483648ull:2147483647ull))return false;
    }
    *out=negative ? (i32)(0u-(u32)val) : (i32)val;
    return true;
}
static bool one_op(instruction_t *op,const char *s,u32 n){
    const char *tail=NULL;u32 op_len=0;bool argument=false;
    for(u32 i=0;i<n;i++)if(s[i]==' '){op_len=i;tail=s+i+1;break;}
    if(!tail)op_len=n;
    struct {const char *name;u8 code;bool arg;} const names[]={
        {"PUSH",OP_PUSH,true},{"ADD",OP_ADD,false},{"SUB",OP_SUB,false},
        {"MUL",OP_MUL,false},{"DUP",OP_DUP,false},{"DROP",OP_DROP,false},
        {"LOAD",OP_LOAD,true},{"STORE",OP_STORE,true},
        {"PRINT",OP_PRINT,false},{"SLEEP",OP_SLEEP,true},
        {"JMP",OP_JMP,true},{"JZ",OP_JZ,true},{"HALT",OP_HALT,false}
    };
    for(u32 i=0;i<sizeof names/sizeof names[0];i++) {
        if(!text_eq(s,op_len,names[i].name))continue;
        op->op=names[i].code;op->arg=0;argument=names[i].arg;
        if(argument) {
            if(!tail||!parse_number(tail,n-op_len-1,&op->arg))return false;
            if ((op->op==OP_LOAD||op->op==OP_STORE) &&
                (op->arg<0 || op->arg>=FVM_MEMORY))return false;
            if(op->op==OP_SLEEP && (op->arg<0 || op->arg>10000))return false;
        } else if(tail)return false;
        return true;
    }
    return false;
}
static bool compile(vm_t *vm,const char *source,u32 length){
    if(!source || length<7 || length>4096 ||
       source[0]!='F'||source[1]!='V'||source[2]!='M'||
       source[3]!='/'||source[4]!='1'||source[5]!='\n')return false;
    vm->count=0;vm->pc=0;vm->sp=0;vm->executed=0;vm->printed=false;
    for(u32 from=6;from<length;){
        u32 to=from;
        while(to<length && source[to]!='\n') {
            if(source[to]<32||source[to]>126||to-from>=180)return false;
            to++;
        }
        if(to==length)return false;
        if(to>from && source[from]!='#'){
            if(vm->count>=FVM_OPS || !one_op(&vm->instructions[vm->count],source+from,to-from))
                return false;
            vm->count++;
        }
        from=to+1;
    }
    if(!vm->count)return false;
    for(u32 i=0;i<vm->count;i++){
        instruction_t *op=&vm->instructions[i];
        if((op->op==OP_JMP||op->op==OP_JZ) &&
           (op->arg<0||op->arg>=vm->count))return false;
    }
    return true;
}
i32 fvm_spawn_source(const char *name,const char *data,u32 length) {
    for(i32 i=0;i<FVM_PROCS;i++){
        if(procs[i].state==VM_UNUSED || procs[i].state==VM_HALTED ||
           procs[i].state==VM_FAILED){
            vm_t *vm=&procs[i];
            k_memset(vm,0,sizeof *vm);
            if(!compile(vm,data,length)) {vm->state=VM_UNUSED;return -1;}
            if(name){
                i32 n=0;
                while(name[n]&&n<SHFS_PATH-1){vm->name[n]=name[n];n++;}
                vm->name[n]=0;
            }
            vm->state=VM_RUNNING;
            return i;
        }
    }
    return -1;
}
i32 fvm_spawn_file(const char *path){
    shfs_ent_t *file=shfs_lookup_rel(shfs_cwd,path);
    if(!file||file->is_dir||file->len>4096)return -1;
    return fvm_spawn_source(file->path,file->data,file->len);
}
static bool push(vm_t *v,i32 x){
    if(v->sp>=FVM_STACK)return false;
    v->stack[v->sp++]=x;return true;
}
static bool pop(vm_t *v,i32 *x){
    if(!v->sp)return false;
    *x=v->stack[--v->sp];return true;
}
static void step(vm_t *v){
    if(v->pc>=v->count || v->executed++>=FVM_TOTAL_STEPS){v->state=VM_FAILED;return;}
    instruction_t op=v->instructions[v->pc++];
    i32 a=0,b=0;
    switch(op.op){
    case OP_PUSH: if(!push(v,op.arg))v->state=VM_FAILED;break;
    case OP_ADD: case OP_SUB: case OP_MUL:
        if(!pop(v,&b)||!pop(v,&a)){v->state=VM_FAILED;break;}
        if(!push(v,op.op==OP_ADD?(i32)((u32)a+(u32)b):
                  op.op==OP_SUB?(i32)((u32)a-(u32)b):
                                 (i32)((u32)a*(u32)b)))v->state=VM_FAILED;
        break;
    case OP_DUP: if(!v->sp||!push(v,v->stack[v->sp-1]))v->state=VM_FAILED;break;
    case OP_DROP: if(!pop(v,&a))v->state=VM_FAILED;break;
    case OP_LOAD: if(!push(v,v->locals[op.arg]))v->state=VM_FAILED;break;
    case OP_STORE:if(!pop(v,&v->locals[op.arg]))v->state=VM_FAILED;break;
    case OP_PRINT:
        if(!pop(v,&a))v->state=VM_FAILED;
        else {v->last_print=a;v->printed=true;}
        break;
    case OP_SLEEP:v->wakeup=g_ticks+(u32)op.arg;v->state=VM_WAITING;break;
    case OP_JMP:  v->pc=(u16)op.arg;break;
    case OP_JZ:
        if(!pop(v,&a))v->state=VM_FAILED;
        else if(a==0)v->pc=(u16)op.arg;
        break;
    case OP_HALT:v->state=VM_HALTED;break;
    default:v->state=VM_FAILED;break;
    }
    if(v->pc==v->count&&v->state==VM_RUNNING)v->state=VM_HALTED;
}
void fvm_tick(void){
    for(i32 i=0;i<FVM_PROCS;i++){
        vm_t *v=&procs[i];
        if(v->state==VM_WAITING && (i32)(g_ticks-v->wakeup)>=0)v->state=VM_RUNNING;
        for(i32 n=0;n<FVM_STEPS_PER_TICK && v->state==VM_RUNNING;n++)step(v);
    }
}
bool fvm_kill(i32 slot){
    if(slot<0||slot>=FVM_PROCS||procs[slot].state==VM_UNUSED)return false;
    procs[slot].state=VM_HALTED;
    return true;
}
i32 fvm_state(i32 slot){return slot>=0&&slot<FVM_PROCS ? (i32)procs[slot].state : -1;}
i32 fvm_last_print(i32 slot){return slot>=0&&slot<FVM_PROCS?procs[slot].last_print:0;}
void fvm_status(char *out,i32 capacity){
    if(!out||capacity<32)return;
    out[0]=0;
    for(i32 i=0;i<FVM_PROCS;i++){
        vm_t *v=&procs[i];if(v->state==VM_UNUSED)continue;
        if((u32)k_strlen(out)+90u>=(u32)capacity)break;
        char tmp[16];
        k_itoa((u32)i,tmp,10);k_strcat(out,"vm ");k_strcat(out,tmp);
        k_strcat(out," ");
        k_strcat(out, v->state==VM_RUNNING?"running":
                         v->state==VM_WAITING?"sleeping":
                         v->state==VM_HALTED?"halted":"failed");
        k_strcat(out," pc=");
        k_itoa(v->pc,tmp,10);k_strcat(out,tmp);
        k_strcat(out," ");k_strcat(out,v->name);
        if(v->printed){k_strcat(out," last=");k_itoa((u32)v->last_print,tmp,10);k_strcat(out,tmp);}
        k_strcat(out,"\n");
    }
    if(!out[0])k_strcpy(out,"no virtual-machine applications");
}
