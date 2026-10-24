#include "../src/key_argument.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
namespace c=button_controls::customizer;
static unsigned checks=0,destroys=0,calls=0,copies=0;
static void expect(bool value,const char* label){++checks;if(!value){std::cerr<<"FAIL "<<label<<'\n';std::exit(1);}}
struct Details{int references=2;}; // Native registry plus one static EKeys value.
struct Key{uint64_t name;Details* details;void* padding;};
static_assert(sizeof(Key)==24);
static void destroy(void* p){auto key=static_cast<Key*>(p);++destroys;--key->details->references;}
static void copy(void* to,const void* from){++copies;*static_cast<Key*>(to)=*static_cast<const Key*>(from);++static_cast<Key*>(to)->details->references;}
int main(){
    Details a_details,x_details;Key a{1,&a_details,nullptr},x{2,&x_details,nullptr};
    auto native=[&](void* p){
        expect(p!=&a && p!=&x,"native callee must never consume a borrowed EKeys global");
        expect(reinterpret_cast<uintptr_t>(p)%8==0,"FKey copy retains native alignment");
        ++calls;const auto name=static_cast<Key*>(p)->name;destroy(p);return name;
    };
    {
        Key arg{};copy(&arg,&a);
        {c::KeyArgument incoming(&arg,destroy);expect(incoming.dispatch(&arg,copy,native)==1,"identity dispatch keeps key");}
        expect(a_details.references==2 && destroys==1 && calls==1,"pass-through transfers exactly one owning argument");
    }
    {
        Key arg{};copy(&arg,&a);const auto before=destroys;
        {c::KeyArgument incoming(&arg,destroy);expect(incoming.dispatch(&x,copy,native)==2,"remapping dispatches the new native key");}
        expect(a_details.references==2 && x_details.references==2 && destroys==before+2,"remap releases both incoming and forwarded copies exactly once");
    }
    {
        Key arg{};copy(&arg,&a);const auto before=calls;
        {c::KeyArgument swallowed(&arg,destroy);}
        expect(a_details.references==2 && calls==before,"swallowed input releases its incoming reference without dispatch");
    }
    expect(c::dispatch_key_copy(&x,copy,native)==2 && x_details.references==2,"synthetic context release consumes a private copy");
    std::mt19937 random(0x464b4559);
    for(int n=0;n<100000;++n){
        Key arg{};const bool from_x=random()%2;copy(&arg,from_x?&x:&a);
        {
            c::KeyArgument input(&arg,destroy);
            switch(random()%3){
                case 0:input.dispatch(&arg,copy,native);break;
                case 1:input.dispatch(from_x?&a:&x,copy,native);break;
                default:break;
            }
        }
        if(random()%7==0)c::dispatch_key_copy(&x,copy,native);
        expect(a_details.references==2 && x_details.references==2,"mixed consume/remap/synthetic events preserve both static key references");
    }
    expect(copies==destroys,"every owning key copy is destroyed exactly once");
    std::cout<<checks<<" native-key lifetime checks passed (100000 mixed argument transfers)\n";
}
