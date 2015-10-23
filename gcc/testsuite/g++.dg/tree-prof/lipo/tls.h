extern int NextId();

class TLSClass {
 public:
  TLSClass() {
    id = NextId();
    bar = 1;
  }
  ~TLSClass() {}
  int id;
  int bar;
};
extern TLSClass* NextTLSClass();
extern void *SetTLSClass(TLSClass *a);
extern TLSClass *GetTLSClass();
extern thread_local TLSClass* current_tls_;
