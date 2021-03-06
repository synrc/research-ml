// ML runtime

#include <string.h>
#include <stdlib.h>

#include "exec.h"

int last_proc = 0;

void  rterror(char *m, alfa lastid) { printf("\nError: %s LastId=%s\n", m, lastid); exit(1); }
value checkval(char *m, values vs, value v, alfa lastid) { if (v->tag & vs == 0) rterror(m, lastid); return v; }

// memory allocators

tree copy(tree source)
{
    if (source == 0) return 0;
    tree n = newnode(source->tag);
    switch (source->tag)
    {
        case ident: strcpy(n->id, source->id); break;
        case intcon: n->n = source->n; break;
        case boolcon: n->b = source->b; break;
        case charcon: n->ch = source->ch; break;
        case lambdaexp: n->lambda.parm = copy(source->lambda.parm); n->lambda.body = copy(source->lambda.body); break;
        case application: n->application.func = copy(source->application.func); n->application.parm = copy(source->application.parm); break;
        case block: n->block.decs = copy(source->block.decs); n->block.expr = copy(source->block.expr); break;
        case decln: n->decln.value = copy(source->decln.value); strcpy(n->decln.name,source->decln.name); break;
        case declist: n->declist.head = copy(source->declist.head); n->declist.tail = copy(source->declist.tail); n->declist.recursive = source->declist.recursive; break;
        case ifexp: n->ifexp.e1 = copy(source->ifexp.e1); n->ifexp.e2 = copy(source->ifexp.e2); n->ifexp.e3 = copy(source->ifexp.e3); break;
        case binexp: 
        case unexp: n->expression.op = source->expression.op; n->expression.left = copy(source->expression.left); 
                                                              n->expression.right = copy(source->expression.right); break;
    }

//    print(4,n);
    return n;
}

value mkvalue(valueclass t) { value p=(value)malloc(sizeof(valnode)); p->tag=t; return p; }
value mkint(int nn)         { value p=(value)malloc(sizeof(valnode)); p->tag=intval; p->n=nn; return p; }
value mkbool(int bb)        { value p=(value)malloc(sizeof(valnode)); p->tag=boolval; p->b=bb; return p; }
value mkchar(char cc)       { value p=(value)malloc(sizeof(valnode)); p->tag=charval; p->ch=cc; return p; }

value mkfunc(tree code, env rho) { value p=(value)malloc(sizeof(valnode)); 
                                   p->tag=funcval; p->func.e=code; p->func.r=rho; return p; }

value defer(tree x, env rho) { value p=(value)malloc(sizeof(valnode)); 
                               p->tag=deferval; p->func.e=x; p->func.r=rho; return p; }

value vcons(value h, value t, int *conscells) { value p=(value)malloc(sizeof(valnode)); 
                                                p->tag=listval; p->list.hd=h; p->list.tl=t; (*conscells)++; return p; }

value mkprocess1(valueclass t, value pa, value pb, alfa lastid, int64 processvalues)
{
    value p=(value)malloc(sizeof(valnode));
    p->tag=t;  //choice or para, | or ||, NB. pA, pB in WHNF
    p->proc.p1=checkval("bad left proc", processvalues, pa, lastid);
    p->proc.p2=checkval("bad right proc", processvalues, pb, lastid);
    return p;
}

value mkprocess2(valueclass t, value chan, tree m, tree c, env rho)
{
    value p=(value)malloc(sizeof(valnode));
    p->tag=t; //choice or para, | or ||, NB. pA, pB in WHNF
    p->ioproc.chan=chan;
    p->ioproc.msg=m;
    p->ioproc.cont=c;
    p->ioproc.pr=rho;
    return p;
}

value mkchannel(int* channelcntr)
{
    value p=(value)malloc(sizeof(valnode));
    p->tag=channelval;  //choice or para, | or ||, NB. pA, pB in WHNF
//    printf("channel %i created\n",*channelcntr);
    p->n = (*channelcntr)++;
    return p;
}

void force(value v, exec_ctx *c);

env bind(alfa x, value val, env r, int* envcells) // :Ide -> Value -> Env -> Env 
{
    env p=(env)malloc(sizeof(binding));
    (*envcells)++;
    strcpy((char*)p->id,(char*)x);
    p->v=val;
    p->next=r;
    return p;
}

value applyenv(env r, alfa x, exec_ctx *c)
{ 
    strcpy(c->lastid,x);
    if (r==0) rterror("undec id", c->lastid);
    else if (!strcmp(r->id,x)) 
    {
        force(r->v, c); //only called from eval
        return r->v;
    }
    else return applyenv(r->next, x, c);
}

env d2(tree decs, env local, env global, int* envcells)
{ 
    if (decs==0) return global;
    else return bind(decs->declist.head->decln.name,
		     defer(decs->declist.head->decln.value,local),
		     d2(decs->declist.tail, local, global, envcells),
		     envcells);
}

env d(tree decs, env rho, int* envcells) // D :Decs -> Env -> Env
{
    env localrho;

    if (decs==0) return rho;
    else
    {
        if (decs->declist.recursive) 
        {
            localrho=bind("dummy", 0, rho, envcells);
            localrho->next=d2(decs, localrho, rho, envcells);
            return localrho;
            }
            else return d2(decs, rho, rho, envcells);
    }
}

value eval(tree x, env rho, exec_ctx *c);

// application of function to argument (beta-reduction)

value apply(value fn, value ap, exec_ctx *c)
{
    if (fn->func.e->lambda.parm->tag == emptycon)   // (L().e)ap 
    {
        force(ap, c);
        if (ap->tag == emptyval) 
            return eval(fn->func.e->lambda.body, fn->func.r, c);
        else
        {
            rterror("L().e exp", c->lastid);
            return 0;
        }
    }
    else return eval(fn->func.e->lambda.body, bind(fn->func.e->lambda.parm->id, ap, fn->func.r, &(c->envcells)), c);
}

// binary function

value o(symbol opr, value v1, value v2, exec_ctx *c)
{
    int abs1, abs2, intans;
    int boolans;
    value o_result;
    switch (opr)
    {
        case parallelsy: //...||...
            o_result = mkprocess1(paraprocessval, v1, v2, c->lastid, c->processvalues);
            break;
        case choicesy:   //...|.... 
            o_result = mkprocess1(choiceprocessval, v1, v2, c->lastid, c->processvalues);
            break;
        case eq: case ne: case lt: case le: case gt: case ge:
            if (1 << v1->tag && 1 << v2->tag && (1 << intval | 1 << charval | 1 << boolval) != 0)
            switch (v1->tag)
            {
                case intval: { abs1=v1->n; abs2=v2->n; } break;
                case boolval: { abs1=(int)(v1->b); abs2=(int)(v2->b); } break;
                case charval: { abs1=(int)(v1->ch); abs2=(int)(v2->ch); } break;
            }
            else
                rterror("relation operation ", c->lastid);
            switch (opr)
            {
                case eq: boolans=abs1==abs2; break; case ne: boolans=abs1!=abs2; break;
                case le: boolans=abs1<=abs2; break; case lt: boolans=abs1< abs2; break;
                case ge: boolans=abs1>=abs2; break; case gt: boolans=abs1> abs2; break;
            }
            o_result=mkbool(boolans);
            break;        
	case plus: case minus: case times: case over:
	    if ( (1 << v1->tag | 1 << v2->tag) == (1 << intval)) 
            switch (opr)
            {
        	case plus:  intans=v1->n + v2->n; break;
                case minus: intans=v1->n - v2->n; break;
                case times: intans=v1->n * v2->n; break;
                case over:  intans=v1->n / v2->n; break;
            }
            else rterror("arithmetic operation ", c->lastid);
            o_result=mkint(intans);
            break;
        case andsy: 
        case orsy:
            if ((1 << v1->tag | 1 << v2->tag) == (1 << boolval)) 
               switch (opr) {
               case andsy: boolans=v1->b & v2->b; break;
               case orsy:  boolans=v1->b | v2->b;
               break;
               }
               else rterror("bool operation ", c->lastid);
               o_result=mkbool(boolans);
    	    break;
        case conssy: // deferred params 
	  o_result=vcons(v1, v2, &(c->conscells));
    	    break;
    }
    return o_result;
}

// unary function

value u(symbol opr, value v, exec_ctx *c)
{
    value u_result;
    switch (opr) {
	case minus: if (v->tag==intval) u_result = mkint(-v->n); else rterror("- non int ", c->lastid); break;
	case notsy: if (v->tag==boolval) u_result = mkbool(~ v->b); else rterror("not ~bool ", c->lastid); break;
	case spawnsy: if (v->tag==funcval) u_result = mkfunc(v->func.e,v->func.r); else rterror("not ~bool ", c->lastid); break;
	case hdsy:  if (v->tag==listval) { force(v->list.hd, c);
					   u_result = v->list.hd; } else rterror("hd ~list  ", c->lastid); break;
	case tlsy:  if (v->tag==listval) { force(v->list.tl, c);
					    u_result = v->list.tl; } else rterror("tl ~list  ", c->lastid); break;
        case nullsy:if (v->tag==listval) u_result=mkbool(0); else if (v->tag == nilval) 
                                u_result=mkbool(1); else rterror("null ~list", c->lastid); break;
    }
    return u_result;
}

void bootstrap(tree prog, exec_ctx *c);

value eval(tree x, env rho, exec_context c)
{
    value func, switch_, chnl;
    valueclass proctag;
    value eval_result;
    tree xx;
    exec_context tail;

    switch (x->tag) {
        case ident:       eval_result=applyenv(rho, x->id, c); break;
        case intcon:      eval_result=mkint(x->n); break;
        case boolcon:     eval_result=mkbool(x->b); break;
        case charcon:     eval_result=mkchar(x->ch); break;
        case nilcon:      eval_result=mkvalue(nilval); break;
        case emptycon:    eval_result=mkvalue(emptyval); break;
        case newchan:     eval_result=mkchannel(&(c->channelcntr)); break;
        case lambdaexp:   eval_result=mkfunc(x, rho); break;
        case application: func = eval(x->application.func, rho, c);
                          if (func->tag==funcval) eval_result = apply(func, defer(x->application.parm, rho), c);
                          else rterror("apply ~fn ", c->lastid);
                          break;
        case unexp:
            if (x->expression.op == spawnsy)
            {
                 c->evals++;
                 exec_context proc = (exec_context)malloc(sizeof(exec_ctx));//proctable[last_proc];
                 proc->next = NULL;
                 while (c->next) c = c->next;
                 c->next = proc;
                 bootstrap(x->expression.left, proc);
                 eval_result->tag = procid;
                 eval_result->n = last_proc;
                 last_proc++;
                 return eval_result;
            } else
                eval_result=u(x->expression.op, eval(x->expression.left, rho, c), c); break;
	case binexp:
	    if (x->expression.op==sequencesy)                   //->
            {
        	if (x->expression.left->tag == binexp) 
                {
            	    if (x->expression.left->expression.op == inputsy)         //...?...->
                    {
                	proctag=inprocessval;
                        if (x->expression.left->expression.right->tag != ident) 
                    	    rterror("...?~var  ", c->lastid);
                    }
                    else if (x->expression.left->expression.op == outputsy)   //...!...->
                    {
                        proctag=outprocessval;
                    } 
                    else rterror("~\?/! ->...", c->lastid);
                    
                    chnl=eval(x->expression.left->expression.left,rho, c);
                    
                    if (chnl->tag != channelval) rterror("chan expected", c->lastid);
                    
                    eval_result=mkprocess2(proctag,chnl,x->expression.left->expression.right,x->expression.right,rho);

        	}
                else rterror("~IO -> ...", c->lastid);
             }
             else if ((x->expression.op & (1 << inputsy | 1 << outputsy)) != 0) 
             {
                     // An action is part of a process, not a Value, ?yet? 
                     // NB. an input action needs a Cont to take in-value  
                     rterror("eval ? | !", c->lastid);

	     }
             else if (x->expression.op==conssy) // cons should not eval ... 
             {
        	eval_result=o(x->expression.op,
        		      defer(x->expression.left,rho),
                              defer(x->expression.right,rho),
                              c);
                              
	     }
             else eval_result=o(x->expression.op, eval(x->expression.left,rho, c), eval(x->expression.right,rho, c), c);
	    break;
	case ifexp:
            switch_=eval(x->ifexp.e1, rho, c);
            if (switch_->tag==boolval) 
            { 
                if (switch_->b) 
                     eval_result=eval(x->ifexp.e2, rho, c);
                else eval_result=eval(x->ifexp.e3, rho, c);
            } else rterror("if ~bool  ", c->lastid);
            break;
	case block:
	    eval_result=eval(x->block.expr,
			     d(x->block.decs, rho, &(c->envcells)),
			     c);
    	    break;
    }   
    c->evals++; // statistics 
    return eval_result;
}

void force(value v, exec_ctx *c)
{
    value fv=(value)malloc(sizeof(valnode));
    if (v->tag == deferval) 
    {
        fv = eval(v->func.e, v->func.r, c);
        *v = *fv;
    }
}

void showvalue(value v, exec_ctx *c)
{
    switch (v->tag)
    {
	case intval:  printf("%1i",  v->n); break;
	case boolval: printf("%1i",  v->b ); break;
        case charval: printf("%c",  v->ch ); break;
        case emptyval:printf("{}" ); break;
        case nilval:  printf("nil"); break;
        case procid:  printf("<0.%i>", v->n); break;
        case listval: { printf("{");
    			showvalue(v->list.hd, c); 
    			printf(",");
    			showvalue(v->list.tl, c); 
    			printf("}"); } break;
        case funcval: printf("function"); break;
        case inprocessval: case outprocessval: case choiceprocessval: case paraprocessval: printf("process"); break;
        case stopprocessval: printf("stop"); break;
        case channelval: printf("chan%1i", v->n); break;
        case deferval:{ force(v, c);
                        showvalue(v, c); } break; // evaluation is o/p driven 
    }
}

// process calculus

int complementary(value *ip, value subip, value *op, value subop, exec_ctx *c)
{
    int chan;
    value val;
    //?can IP and OP interact?
    int complementary_result = 0; //unless
    chan = subip->ioproc.chan->n;

    if (chan == subop->ioproc.chan->n)  //exchange msg
    {
        complementary_result = 1;
        if (chan == 1) //input 
        {         //val:= ...
            rterror("not impl  ", c->lastid);
        } else val=defer(subop->ioproc.msg,subop->ioproc.pr);

        if (chan==0)
        {
            printf(" output ");
            showvalue(val, c); 
            printf("\n"); 
        }
        else
        {
            printf(" ch%1i ", chan);
            showvalue(val, c);
        }

        *op = eval(subop->ioproc.cont, subop->ioproc.pr, c);
        *ip = eval(subip->ioproc.cont, bind(subip->ioproc.msg->id,val,subip->ioproc.pr, &(c->envcells)), c);
    }

    return complementary_result;
}

int traverseop(value *ip, value subip, value *op, value subop, exec_ctx *c) // find an output process
{ 
    int traverseop_result = 0;
    switch (subop->tag)
    {
	case inprocessval:     break;
	case outprocessval:    traverseop_result = complementary(ip, subip, op, subop, c); break;
	case stopprocessval:   break; //can't interact!
	case paraprocessval:   rterror("|| under |", c->lastid); break;
	case choiceprocessval: if (traverseop(ip, subip, op, subop->proc.p1, c)) traverseop_result=1;
                               else traverseop_result=traverseop(ip,subip,op,subop->proc.p2,c); break;
    }
    return traverseop_result;
}

int findop(value *ip, value subip, value *op, exec_ctx *c)
{ 
    int findop_result;
    if ((*op)->tag==paraprocessval)   //p1||p2
    {
        if (findop(ip, subip, &((*op)->proc.p1), c)) findop_result=1;
        else findop_result = findop(ip, subip, &((*op)->proc.p2), c);
    } 
    else 
    {
      findop_result = traverseop(ip, subip, op, *op, c);
    }
    //p1|p2, a->p, stop
    return findop_result;
}
                                                  //IP - Input Process
int traverseip(value *ip, value subip, exec_ctx *c) //find an input process
{     
    int traverseip_result = 0;
    switch (subip->tag)
    {
         case inprocessval:     traverseip_result = findop(ip, subip, &(c->processes), c); break;
         case outprocessval:    break;
         case stopprocessval:   break; // can't interact!
         case paraprocessval:   rterror("|| under |", c->lastid); break;
         case choiceprocessval: if  (traverseip(ip, subip->proc.p1, c)) traverseip_result=1;
                                else traverseip_result = traverseip(ip, subip->proc.p2, c); break;
    }
    return traverseip_result;
}

int findip(value *ip, exec_ctx *c)
{
    int findip_result = 0;
    if ((*ip)->tag==paraprocessval)    //p1||p2
    {
        if (findip(&((*ip)->proc.p1), c) == 1) findip_result=1;
        else findip_result=findip(&((*ip)->proc.p2), c);
        
    }
    else 
    {
      findip_result=traverseip(ip, *ip, c);
    }
    //p1|p2, a->p, stop
    return findip_result;
}  

int interact(exec_ctx *c) 
{ 
    int interact_result;
    interact_result = findip(&(c->processes), c);
    return interact_result;
}

int count(value p, int64 processvalues, alfa lastid)
{
    int count_result;
    if ((p->tag && processvalues) != 0) 
    switch (p->tag)
    {
        case inprocessval: 
        case outprocessval:    count_result=1; break;
        case stopprocessval:   count_result=0; break;
        case paraprocessval:   count_result = count(p->proc.p1, processvalues, lastid) + 
                                              count(p->proc.p2, processvalues, lastid); break;
        case choiceprocessval: count_result = (int)(count(p->proc.p1, processvalues, lastid) + 
                                                    count(p->proc.p2, processvalues, lastid) > 0); break;
    }
    else rterror("in count ", lastid);
    return count_result;
}

void bootstrap(tree prog, exec_ctx *c)
{
    c->evals = 0; c->envcells = 0; c->conscells = 0;    //zero counters
    strcpy(c->lastid,"start     ");
    c->channelcntr = 0;
    c->processvalues = 1 << inprocessval |
                       1 << outprocessval | 
                       1 << choiceprocessval |
                       1 << paraprocessval |
                       1 << stopprocessval;

    c->outputchan = mkchannel(&(c->channelcntr));
    c->outputmsg=(tree)malloc(sizeof(node));
    c->outputmsg->tag=ident;
    strcpy(c->outputmsg->id,"x         "); 
    c->outputcont=(tree)malloc(sizeof(node));
    c->outputcont->tag=ident;
    strcpy(c->outputcont->id,"outputProc"); 
    c->outputproc = mkprocess2(inprocessval,c->outputchan,c->outputmsg,c->outputcont,(env)0);
    c->inputchan  = mkchannel(&(c->channelcntr));
    c->inputmsg=(tree)malloc(sizeof(node));
    c->inputmsg->tag=emptycon;
    c->inputcont=(tree)malloc(sizeof(node));
    c->inputcont->tag=ident;
    strcpy(c->inputcont->id,"inputProc ");
    c->inputproc  = mkprocess2(outprocessval,c->inputchan,c->inputmsg,c->inputcont,(env)0);

    c->sysenv = bind("output    ", c->outputchan,
	        bind("outputProc", c->outputproc,
	        bind("input     ", c->inputchan,
                bind("inputProc ", c->inputproc,
                bind("stop      ", mkvalue(stopprocessval),
    	        (env)0,
		     &(c->envcells)), &(c->envcells)), &(c->envcells)), &(c->envcells)), &(c->envcells));
    	    
     c->outputproc->ioproc.pr=c->sysenv;
     c->inputproc->ioproc.pr=c->sysenv;

     c->processes =
         mkprocess1(
               paraprocessval,
               c->outputproc,
               mkprocess1(
                   paraprocessval,
                   eval(prog, c->sysenv, c),
                   c->inputproc,
                   c->lastid,
                   c->processvalues),
              c->lastid,
              c->processvalues);

//    printf("--------------\n");
//    print(0, prog);
//    printf("\n--------------");

}

void execute(tree prog, exec_context c)
{
    int i = 0, xxx = 0;
    int alive = 1;
    exec_context proc;

    last_proc = 1;

    bootstrap(prog, c);

    while (alive)
    {
        alive = 0;
        proc = c;
        while (proc) { if (interact(proc)) alive = 1; proc = proc->next; }
    }

    c->n=count(c->processes, c->processvalues, c->lastid);

    printf("\n"); printf("%i processes left",  c->n); if (c->n>2) printf(" (deadlock)");
    printf("\n"); printf("%i evals, ",  c->evals);
    printf("procs: %i\n", last_proc);
    printf("%i env cells used, ",  c->envcells);
    printf("%i cells used\n",  c->conscells);
}

