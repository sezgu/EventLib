# EventLib
C++ event library with .Net like use characteristics

Main features of EventLib
- Has no dependencies and composed of 1 header
- Easy to start using since it has the same syntactical sugar that .Net provides.
- Thread safe and lock free
- Completely RAII (subscriptions returns objects and disposal of subscription object is an action of unsubscribe)
- Supports free functions, lambdas and methods.
- Has 3 behaviors regarding event result handling. Default is returning the result of last event call (as .Net)
- A type can expose an event with any access modifier with only 1 line ( with EVENT macro, very similar to .Net)
