#pragma once

/**
  Execute a block of code once (created/assigned) variable leaves scope

  Example usage

    auto Cleanup = = MakeScopeExit([](){
        ... do something ...
    });

  So when 'Cleanup' variable leaves scope, the block
  of code will execute

  From:

    https://gist.github.com/prideout/2ef4987ed253daa7abcb


*/

namespace hotbits
{

  template <typename F>
  struct ScopeExit
  {
    ScopeExit(F f) : f(f) {}
    ~ScopeExit() { f(); }
    F f;
  };

  template <typename F>
  ScopeExit<F> MakeScopeExit(F f)
  {
    return ScopeExit<F>(f);
  };

};