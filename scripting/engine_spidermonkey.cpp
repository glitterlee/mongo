// engine_spidermonkey.cpp

#include "engine.h"

#ifdef _WIN32
#define XP_WIN
#else
#define XP_UNIX
#endif

#ifdef MOZJS
#include "mozjs/jsapi.h"
#else
#include "js/jsapi.h"
#endif

namespace mongo {

    JSBool resolveBSONField( JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp );

    static JSClass bson_ro_class = {
        "bson_object" , JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE , 
        JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
        JS_EnumerateStub, (JSResolveOp)(&resolveBSONField) , JS_ConvertStub, JS_FinalizeStub,
        JSCLASS_NO_OPTIONAL_MEMBERS
    };


    class Convertor {
    public:
        Convertor( JSContext * cx ){
            _context = cx;
        }
        
        string toString( JSString * so ){
            jschar * s = JS_GetStringChars( so );
            size_t srclen = JS_GetStringLength( so );
            
            size_t len = srclen * 2;
            char * dst = (char*)malloc( len );
            assert( JS_EncodeCharacters( _context , s , srclen , dst , &len) );
            
            string ss( dst , len );
            free( dst );
            return ss;
        }

        string toString( jsval v ){
            return toString( JS_ValueToString( _context , v ) );            
        }

        // ---------- to spider monke ---------
        
        jsval toval( double d ){
            jsval val;
            assert( JS_NewNumberValue( _context, d , &val ) );
            return val;
        }

        jsval toval( const char * c ){
            JSString * s = JS_NewStringCopyZ( _context , c );
            return STRING_TO_JSVAL( s );
        }

        JSObject * toObject( const BSONObj * obj , bool readOnly=true ){
            // TODO: make a copy and then delete it
            JSObject * o = JS_NewObject( _context , &bson_ro_class , NULL, NULL);
            JS_SetPrivate( _context , o , (void*)obj );
            return o;
        }
        
        jsval toval( const BSONObj* obj , bool readOnly=true ){
            JSObject * o = toObject( obj , readOnly );
            return OBJECT_TO_JSVAL( o );
        }
        
    private:
        JSContext * _context;
    };

    static JSClass global_class = {
        "global", JSCLASS_GLOBAL_FLAGS,
        JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
        JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
        JSCLASS_NO_OPTIONAL_MEMBERS
    };    
    
    JSBool resolveBSONField( JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp ){
        Convertor c( cx );
        
        BSONObj * o = (BSONObj*)(JS_GetPrivate( cx , obj ));
        string s = c.toString( id );
        
        jsval val;

        BSONElement e = (*o)[ s.c_str() ];
        switch( e.type() ){
        case EOO:
            *objp = 0;
            return JS_TRUE;
        case NumberDouble:
        case NumberInt:
            val = c.toval( e.number() );
            break;
        case String:
            val = c.toval( e.valuestr() );
            break;
        default:
            log() << "resolveBSONField can't handle type: " << (int)(e.type()) << endl;
            uassert( "not done" , 0 );
            return JS_FALSE;
        }

        assert( JS_SetProperty( cx , obj , s.c_str() , &val ) );
        *objp = obj;
        return JS_TRUE;
    }
    

    class SMScope;
    
    class SMEngine : public ScriptEngine {
    public:
        
        SMEngine(){
            _runtime = JS_NewRuntime(8L * 1024L * 1024L);
            uassert( "JS_NewRuntime failed" , _runtime );

        }

        ~SMEngine(){
            JS_DestroyRuntime( _runtime );
            JS_ShutDown();
        }

        Scope * createScope();
        
        void runTest();
        
    private:
        JSRuntime * _runtime;
        friend class SMScope;
    };
    
    SMEngine * globalSMEngine;


    void ScriptEngine::setup(){
        globalSMEngine = new SMEngine();
        globalScriptEngine = globalSMEngine;
    }


    class SMScope : public Scope {
    public:
        SMScope(){
            _context = JS_NewContext( globalSMEngine->_runtime , 8192 );
            _convertor = new Convertor( _context );
            massert( "JS_NewContext failed" , _context );
            
            JS_SetOptions( _context , JSOPTION_VAROBJFIX);
            //JS_SetVersion( _context , JSVERSION_LATEST); TODO
            //JS_SetErrorReporter( _context , reportError); TODO
            
            _global = JS_NewObject( _context , &global_class, NULL, NULL);
            massert( "JS_NewObject failed for global" , _global );
            massert( "js init failed" , JS_InitStandardClasses( _context , _global ) );

            _this = 0;
        }

        ~SMScope(){
            delete _convertor;
            JS_DestroyContext( _context );
        }
        
        void reset(){
            massert( "not implemented yet" , 0 );
        }
        
        void init( BSONObj * data ){
            massert( "not implemented yet" , 0 );            
        }

        // ----- getters ------
        double getNumber( const char *field ){
            jsval val;
            assert( JS_GetProperty( _context , _global , field , &val ) );
            return toNumber( val );
        }
        
        double toNumber( jsval v ){
            double d;
            uassert( "not a number" , JS_ValueToNumber( _context , v , &d ) );
            return d;
        }

        string getString( const char *field ){
            jsval val;
            assert( JS_GetProperty( _context , _global , field , &val ) );
            JSString * s = JS_ValueToString( _context , val );
            return _convertor->toString( s );
        }

        bool getBoolean( const char *field ){
            jsval val;
            assert( JS_GetProperty( _context , _global , field , &val ) );

            JSBool b;
            assert( JS_ValueToBoolean( _context, val , &b ) );

            return b;
        }
        
        BSONObj getObject( const char *field ){
            massert( "not implemented yet: getObject()" , 0 ); throw -1;  
        }

        int type( const char *field ){
            massert( "not implemented yet: type()" , 0 ); throw -1;  
        }

        // ----- setters ------
        
        void setNumber( const char *field , double val ){
            jsval v = _convertor->toval( val );
            assert( JS_SetProperty( _context , _global , field , &v ) );
        }

        void setString( const char *field , const char * val ){
            jsval v = _convertor->toval( val );
            assert( JS_SetProperty( _context , _global , field , &v ) );
        }

        void setObject( const char *field , const BSONObj& obj ){
            jsval v = _convertor->toval( &obj );
            JS_SetProperty( _context , _global , field , &v );
        }

        void setBoolean( const char *field , bool val ){
            jsval v = BOOLEAN_TO_JSVAL( val );
            assert( JS_SetProperty( _context , _global , field , &v ) );            
        }

        void setThis( const BSONObj * obj ){
            _this = _convertor->toObject( obj );
        }

        // ---- functions -----
        
        JSFunction * compileFunction( const char * code ){
            if ( strstr( code , "function(" ) != code )
                return JS_CompileFunction( _context , 0 , "anonymous" , 0 , 0 , code , strlen( code ) , "nofile" , 0 );

            // TODO: there must be a way in spider monkey to do this - this is a total hack

            string s = "return ";
            s += code;
            s += ";";

            JSFunction * func = JS_CompileFunction( _context , 0 , "anonymous" , 0 , 0 , s.c_str() , strlen( s.c_str() ) , "nofile" , 0 );
            jsval ret;
            JS_CallFunction( _context , 0 , func , 0 , 0 , &ret );
            return JS_ValueToFunction( _context , ret );
        }

        ScriptingFunction createFunction( const char * code ){
            return (ScriptingFunction)compileFunction( code );
        }
        
        int invoke( JSFunction * func , const BSONObj& args ){
            jsval rval;
            JS_CallFunction( _context , _this , func , 0 , 0 , &rval );
            assert( JS_SetProperty( _context , _global , "return" , &rval ) );
            return 0;
        }

        int invoke( ScriptingFunction funcAddr , const BSONObj& args ){
            return invoke( (JSFunction*)funcAddr , args );
        }

    private:
        JSContext * _context;
        Convertor * _convertor;

        JSObject * _global;
        JSObject * _this;
    };

    void SMEngine::runTest(){
        // this is deprecated
    }

    Scope * SMEngine::createScope(){
        return new SMScope();
    }

}
