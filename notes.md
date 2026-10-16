# Notes
## Nokutoka Momiji

Just a document to write down any notes regarding Katane, lol...

### Order of plans
1. Finish porting native signatures to `KTN_ObjSignature`.
1. Implement primitive kata methods.
1. Implement kata types.
1. Implement cooperative async.
1. Implement module system.
1. Begin building stdlib.

### Signatures
Got to finish porting over signatures to `KTN_ObjSignature`. Honestly, there's not much to say about it. When it comes to the entire system, it has already been fully implemented. The only thing that is missing is an easy way to create them from the C side.
I'll be writing some macros to make it easy for me to do that. Hopefully, this will mean that, even if I change the underlying implementation, that everything will still work A-OK.

After this, I can finally move towards implementing 

### Primitive methods
I am going to be completely honest right off the bat. I'm going to be nabbing a lot of the logic from Dart because I think that their methods are actually pretty nice and pretty helpful.

You can find them in lib/core.ktn.

### Kata variable initialization
I want Dart-style kata property initialization, like such:
```dart
class MyClass {
    final int number;
    final int radix;

    MyClass(this.number, {required this.radix});
}
```

The Katane equivalent is quite similar:
```katane
kata MyKata {
    final mochi number: Int;
    final mochi radix: Int;
    
    MyKata(this.number, {this.radix});
}
```

Problem? Uhh... well, it's a very different thing. Currently, the system works because everything is actually a positional value on the stack assigned to a local variable. So we have to figure out how to not get it into a local variable, but rather to an instance property. This has to happen in the code itself, because the instance has to exist. CallValidate cannot do anything yet because no instance exists at the moment.

### Kata Types
I had the cool idea of adding different kata types. Here are the supported types:
- Normal: Your default kata. Instantiable. Extendable. Inherits everything. Everything must be implemented.
- Abstract: Cannot be instantiated. May or may not have default behavior. Methods with no implementation must be implemented.
- Interface: Cannot be instantiated. **Cannot** have default behavior. Everything must be implemented. Think of them as blueprints for a kata.
- Sealed: Can be either normal, abstract, or an interface, and gets the corresponding behavior. Limited to only the current compilation unit. 
- Extension: Cannot be instantiated. Only adds extra behavior. Cannot override existing behavior.

The following combinations are allowed:
- Abstract Final: Kata cannot be instantiated and cannot be extended further, but has implementation.
- Sealed Abstract: Kata cannot be instantiated, can be extended further but only on the same compilation unit.
- Sealed Interface: Kata cannot be instantiated, has no default behavior, can only be implemented in the same compilation unit.

The following combinations are **NOT** allowed:
- Interface Final: Literally would have zero behavior and not be able to be implemented, which defeats the purpose.
- Sealed (anything) Final: Defeats the whole purpose of sealing a class. 
- Extension (anything): Extensions' whole purpose is to extend behavior. Makes no sense to add any modifiers.
- Abstract Interface: Interfaces are already abstract in the sense that they cannot be instantiated. They have the added condition that all elements must be implemented.

**Note on Mixins**: Uuuuuh... I am well aware that mixins exist. I've seen them in Dart. But I legit have zero idea as to how they are supposed to work or what makes them any different from the existing functionalities. Either way, it can be added in the future.

**Implementation detail**: I have extended katas with a `uint8_t kataType` variable in the struct. This way, you can create all possible combinations by ORing the corresponding `KTN_KataTypes` enum values together (i.e. `KTN_KATA_TYPE_ABSTRACT | KTN_KATA_TYPE_FINAL`)

Compiler already handles valid and invalid combinations. `OP_CLASS` already handles reading the extra type byte after the name. The only issue is that we have not yet implemented the behavior. It should go inside `OP_INHERIT`. At that point, we have access both to the child kata and the parent kata, so checking values and behaviors and acting accordingly should be simple enough. 

As for interfaces, my idea is I can emit multiple OP_INHERIT. The idea is that a kata that inherits from an interface doesn't get a registered sokata. Therefore, if a kata has a non-NULL sokata, we error out on the subsequent OP_INHERIT. This is plausible because interfaces provide zero behavior. They only provide what are essentially the signatures that other classes must contain and implement. Therefore, there is no parent method to call.

Here's an example ported over to Katane (pseudocode) from one of my Dart projects:
(It's a fun exercise in creativity!)

```katane
kata PaginatedViewModel<T> : ChangeNotifier {
    final mochi items: Array<T> = [];
    mochi isLoading: Bool = false;
    mochi hasReachedEnd: Bool = false;

    private mochi _nextOffset = 0;

    shiki fetchPage(offset: Int): Vow<LoadResult<T>>;

    shiki loadMore() {
        if (this.isLoading || this.hasReachedEnd) return;

        this.isLoading = true;
        this.notifyListeners();

        final mochi result = await (spawn fetchPage(this._nextOffset));

        this.items.addAll(result.items);

        this.hasReachedEnd = result.hasReachedEnd;
        this._nextOffset = result.nextOffset;

        this.isLoading = false;
        this.notifyListeners();
    }

    shiki refresh() {
        this.items.clear();
        this.isLoading = false;
        this.hasReachedEnd = false;
        this._nextOffset = 0;

        this.notifyListeners();

        await (spawn this.loadMore());
    }
}

kata ArtistListViewModel : PaginatedViewModel<Artist> {
    final mochi repository: ArtistRepository;
    private mochi _query: String?;

    ArtistListViewModel(this.repository);

    shiki search(query: String?) {
        this._query = (query == null || query.trim().isEmpty) ? null : query.trim();
        this.refresh();
    }

    shiki addArtist(artist: Artist) {
        final mochi repository = this.repository;
        await (spawn repository.saveArtist(artist));
        this.refresh();
    }

    shiki deleteArtist(artist: Artist) {
        final mochi repository = this.repository;
        await (spawn repository.deleteArtist(artist.id));
        this.refresh();
    }

    shiki fetchPage(offset: Int): Vow<LoadResult<Artist>> {
        final mochi query = this._query;
        final mochi repository = this.repository;

        return spawn (
            (query == null) ? repository.getArtistsPage(
                offset: offset
            ) : repository.fuzzySearchArtistsPage(
                query,
                offset: offset
            )
        );
    }
}
```

### Asynchronous Programming
I think it would be really poggers if we could add async programming on Katane. So, here's my idea: V1 of async will be cooperative multitasking via stackful coroutines. Here's the plan:
1. Abstract runtime away from `KTN_VM` into `KTN_Fiber`. The VM now gets a pointer to the current running fiber.
1. 
